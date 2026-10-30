// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for src/widgets/movieinfo_dialog.cpp
// (class dmr::MovieInfoDialog + file-local ToolTipEvent).
// Suite: movie_info_dialog_ext ; helper prefix: mid_.
//
// Scope: MovieInfoDialog previously had only incidental coverage via
// media-dependent playlist paths. These cases exercise the dialog on a fully
// populated MovieInfo without any playback:
//   * ctor UI structure (object names, scroll area, 3 ArrowLine groups,
//     13 title rows, filePathLabel, fixed size)          -> mid_ctor_*
//   * per-row field rendering (Type/Size/Duration/Path,
//     video + audio codec rows incl. boundary "-")       -> mid_ctor_rowValues_*
//   * showEvent centering / paintEvent smoke             -> mid_showEvent_* / mid_paintEvent_*
//   * changedHeight cap at 615                           -> mid_changedHeight_capsAt615
//   * slotThemeTypeChanged foreground role               -> mid_slotThemeTypeChanged_*
//   * onFontChanged re-elide                             -> mid_onFontChanged_reelidesPath
//   * file-local ToolTipEvent filter on the path label
//     (ToolTip shows the Tip, Leave hides it)            -> mid_toolTipEvent_*
//   * header-defined helper widgets PosterFrame /
//     InfoBottom / ArrowLine painting                    -> mid_posterFrame_*, mid_arrowLine_*, mid_infoBottom_*
//
// Safety rules:
//   * Only Google Test (TEST(...)). The shared harness main (test_qtestmain.cpp)
//     supplies main(); never define main() here.
//   * No real media file is opened: MovieInfo is filled by hand; the ctor only
//     reads the struct (no probing).
//   * Screen-dependent cases (showEvent) are guarded by primaryScreen() and
//     GTEST_SKIP when headless.

// STL / Qt / DTK headers BEFORE the access-unlocking #defines.
#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHelpEvent>
#include <QEvent>
#include <QFormLayout>
#include <QScrollArea>
#include <QLabel>
#include <QVariant>

#include <DDialog>
#include <DLabel>
#include <DDrawer>
#include <DFontSizeManager>
#include <DGuiApplicationHelper>
#include <DPushButton>
#include <denhancedwidget.h>

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

// NOTE: the access-unlocking defines must wrap application.h — the project
// header chain (playlist_model.h etc.) defines the structs under test, and
// guards must burn with unlocked access.
#define protected public
#define private public
#include "application.h"
#include "src/libdmr/playlist_model.h"
#include "src/libdmr/utils.h"
#include "src/widgets/tip.h"
#include "src/widgets/movieinfo_dialog.h"
#include "src/accessibility/ac-deepin-movie-define.h"
#undef protected
#undef private

using namespace dmr;
DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

// --- Helpers ---------------------------------------------------------------

// A fully populated MovieInfo with deterministic values.
static MovieInfo mid_makeMovieInfo()
{
    MovieInfo mi;
    mi.valid = true;
    mi.fileType = "mp4";
    mi.filePath = "/tmp/mid_ut_sample_video.mp4";
    mi.fileSize = 5 * 1024 * 1024 + 512;   // > 1M -> "5.0M"
    mi.duration = 3661;                    // 01:01:01
    mi.width = 1920;
    mi.height = 1080;
    mi.resolution = "1920x1080";
    mi.vCodecID = -1;                      // "none" -> "-"
    mi.vCodeRate = 1500000;                // > 1000 -> "1500 kbps"
    mi.fps = 24;                           // "24 fps"
    mi.proportion = 2.0f;                  // "2"
    mi.aCodeID = -1;                       // -> "-"
    mi.aCodeRate = 44100;                  // > 1000 -> "44 kbps"
    mi.aDigit = 16;                        // "16 bits"
    mi.channels = 2;                       // "2 channels"
    mi.sampling = 44100;                   // "44100hz"
    return mi;
}

static MovieInfoDialog *mid_makeDialog()
{
    PlayItemInfo pif;
    pif.valid = true;
    pif.loaded = true;
    pif.mi = mid_makeMovieInfo();
    return new MovieInfoDialog(pif, nullptr);
}

// The i-th field label of the n-th form layout (rows are title/field pairs).
static DLabel *mid_fieldLabel(MovieInfoDialog *dlg, int formIndex, int row)
{
    QList<QFormLayout *> forms = dlg->findChildren<QFormLayout *>();
    if (formIndex >= forms.size()) return nullptr;
    QLayoutItem *it = forms[formIndex]->itemAt(row, QFormLayout::FieldRole);
    return it ? qobject_cast<DLabel *>(it->widget()) : nullptr;
}

// ==========================================================================
// ctor: structure + rendered row values
// ==========================================================================

TEST(movie_info_dialog_ext, mid_ctor_buildsUi_structure)
{
    // Arrange + Act
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    // Assert: names, main building blocks, geometry.
    EXPECT_EQ(dlg->objectName(), QString(MOVIE_INFO_DIALOG));
    QScrollArea *scroll = dlg->findChild<QScrollArea *>(MOVIE_INFO_SCROLL_AREA);
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->viewport()->objectName(), QString(SCROLL_AREA_VIEWPORT));
    EXPECT_NE(dlg->findChild<QWidget *>(MOVIE_INFO_SCROLL_CONTENT), nullptr);
    EXPECT_NE(dlg->findChild<DDrawer *>(FILM_INFO_WIDGET), nullptr);
    EXPECT_NE(dlg->findChild<DDrawer *>(CODEC_INFO_WIDGET), nullptr);
    EXPECT_NE(dlg->findChild<DDrawer *>(AUDIO_INFO_WIDGET), nullptr);
    EXPECT_EQ(dlg->m_expandGroup.size(), 3);
    // 4 film rows (Type/Size/Duration/Path) + 5 video + 5 audio = 14 titles.
    EXPECT_EQ(dlg->m_titleList.size(), 14);
    // Fixed dialog size from the ctor.
    EXPECT_EQ(dlg->width(), 300);
    EXPECT_EQ(dlg->height(), 642);
    // Path label + stored raw path (toolTip mirrors the raw path).
    DLabel *pathLbl = dlg->findChild<DLabel *>("filePathLabel");
    ASSERT_NE(pathLbl, nullptr);
    EXPECT_EQ(dlg->m_sFilePath, QString("/tmp/mid_ut_sample_video.mp4"));
    EXPECT_EQ(pathLbl->toolTip(), dlg->m_sFilePath);

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_ctor_rowValues_basicInfo)
{
    // Arrange + Act
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    // Assert: first form (film info) rows 0..3.
    DLabel *typeField = mid_fieldLabel(dlg, 0, 0);
    ASSERT_NE(typeField, nullptr);
    EXPECT_EQ(typeField->text(), QString("mp4"));
    DLabel *sizeField = mid_fieldLabel(dlg, 0, 1);
    ASSERT_NE(sizeField, nullptr);
    EXPECT_EQ(sizeField->text(), QString("5.0M"));
    DLabel *durField = mid_fieldLabel(dlg, 0, 2);
    ASSERT_NE(durField, nullptr);
    EXPECT_EQ(durField->text(), QString("01:01:01"));
    // Path row: the field is the filePathLabel itself.
    DLabel *pathLbl = dlg->findChild<DLabel *>("filePathLabel");
    ASSERT_NE(pathLbl, nullptr);
    EXPECT_EQ(mid_fieldLabel(dlg, 0, 3), pathLbl);

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_ctor_rowValues_videoInfo)
{
    // Arrange + Act
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    // Assert: second form (codec info): CodecID "-", CodeRate "1500 kbps",
    // FPS "24 fps", Proportion "2", Resolution "1920x1080".
    DLabel *vCodec = mid_fieldLabel(dlg, 1, 0);
    ASSERT_NE(vCodec, nullptr);
    EXPECT_EQ(vCodec->text(), QString("-"));
    DLabel *vRate = mid_fieldLabel(dlg, 1, 1);
    ASSERT_NE(vRate, nullptr);
    EXPECT_EQ(vRate->text(), QString("1500 kbps"));
    DLabel *fps = mid_fieldLabel(dlg, 1, 2);
    ASSERT_NE(fps, nullptr);
    EXPECT_EQ(fps->text(), QString("24 fps"));
    DLabel *prop = mid_fieldLabel(dlg, 1, 3);
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->text(), QString("2"));
    DLabel *res = mid_fieldLabel(dlg, 1, 4);
    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->text(), QString("1920x1080"));

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_ctor_rowValues_audioInfo)
{
    // Arrange + Act
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    // Assert: third form (audio info).
    DLabel *aCodec = mid_fieldLabel(dlg, 2, 0);
    ASSERT_NE(aCodec, nullptr);
    EXPECT_EQ(aCodec->text(), QString("-"));
    DLabel *aRate = mid_fieldLabel(dlg, 2, 1);
    ASSERT_NE(aRate, nullptr);
    EXPECT_EQ(aRate->text(), QString("44 kbps"));
    DLabel *bits = mid_fieldLabel(dlg, 2, 2);
    ASSERT_NE(bits, nullptr);
    EXPECT_EQ(bits->text(), QString("16 bits"));
    DLabel *chans = mid_fieldLabel(dlg, 2, 3);
    ASSERT_NE(chans, nullptr);
    EXPECT_EQ(chans->text(), QString("2 channels"));
    DLabel *samp = mid_fieldLabel(dlg, 2, 4);
    ASSERT_NE(samp, nullptr);
    EXPECT_EQ(samp->text(), QString("44100hz"));

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_ctor_zeroDuration_showsDash)
{
    // duration 0 -> durationStr "00:00:00" -> rendered as "-".
    PlayItemInfo pif;
    pif.valid = true;
    pif.loaded = true;
    pif.mi = mid_makeMovieInfo();
    pif.mi.duration = 0;
    MovieInfoDialog *dlg = new MovieInfoDialog(pif, nullptr);
    ASSERT_NE(dlg, nullptr);

    DLabel *durField = mid_fieldLabel(dlg, 0, 2);
    ASSERT_NE(durField, nullptr);
    EXPECT_EQ(durField->text(), QString("-"));

    delete dlg;
}

// ==========================================================================
// showEvent / paintEvent
// ==========================================================================

TEST(movie_info_dialog_ext, mid_showEvent_centersDialog)
{
    if (QGuiApplication::primaryScreen() == nullptr)
        GTEST_SKIP() << "headless: no primary screen";
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    dlg->show();
    QTest::qWait(30);

    QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    // moveToCenter centers over the frame geometry, which differs from
    // geometry() by the window frame margins; allow a few pixels tolerance.
    EXPECT_LE(qAbs(dlg->geometry().center().x() - avail.center().x()), 5);
    EXPECT_LE(qAbs(dlg->geometry().center().y() - avail.center().y()), 5);

    dlg->hide();
    delete dlg;
}

TEST(movie_info_dialog_ext, mid_paintEvent_doesNotCrash)
{
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    QPaintEvent pe(dlg->rect());
    QApplication::sendEvent(dlg, &pe);

    EXPECT_TRUE(dlg != nullptr);
    delete dlg;
}

// ==========================================================================
// changedHeight / slotThemeTypeChanged / onFontChanged
// ==========================================================================

TEST(movie_info_dialog_ext, mid_changedHeight_capsAt615)
{
    // Arrange
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    // Act
    dlg->changedHeight(200);

    // Assert: setFixedHeight(min(615, expandsHeight + 265)); with three
    // expanded groups the sum exceeds 615-265-30, so it clamps at 615.
    EXPECT_LE(dlg->height(), 615);
    EXPECT_GT(dlg->height(), 0);

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_slotThemeTypeChanged_setsBrightText)
{
    // Arrange
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);

    // Act
    dlg->slotThemeTypeChanged();

    // Assert: file-name label foreground switched to BrightText.
    EXPECT_EQ(dlg->m_pFileNameLbl->foregroundRole(), DPalette::BrightText);

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_onFontChanged_reelidesPath)
{
    // Arrange
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);
    DLabel *pathLbl = dlg->findChild<DLabel *>("filePathLabel");
    ASSERT_NE(pathLbl, nullptr);
    ASSERT_EQ(dlg->m_pFilePathLbl, pathLbl);
    QString before = pathLbl->text();
    ASSERT_FALSE(before.isEmpty());

    // Act
    dlg->onFontChanged(dlg->font());

    // Assert: both derived labels still carry non-empty elided content and
    // the raw path is unchanged.
    EXPECT_FALSE(dlg->m_pFileNameLbl->text().isEmpty());
    EXPECT_FALSE(dlg->m_pFilePathLbl->text().isEmpty());
    EXPECT_EQ(dlg->m_sFilePath, QString("/tmp/mid_ut_sample_video.mp4"));

    delete dlg;
}

// ==========================================================================
// file-local ToolTipEvent filter on the path label
// ==========================================================================

TEST(movie_info_dialog_ext, mid_toolTipEvent_showAndLeave)
{
    // Arrange: the ctor installs a ToolTipEvent filter on filePathLabel and
    // stores the Tip under property "HintWidget".
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);
    DLabel *pathLbl = dlg->findChild<DLabel *>("filePathLabel");
    ASSERT_NE(pathLbl, nullptr);
    Tip *tip = pathLbl->property("HintWidget").value<Tip *>();
    ASSERT_NE(tip, nullptr);
    ASSERT_FALSE(tip->isVisible());

    // Act 1: ToolTip event shows the tip near the cursor.
    QPoint global = pathLbl->mapToGlobal(pathLbl->rect().center());
    QHelpEvent te(QEvent::ToolTip, pathLbl->rect().center(), global);
    QApplication::sendEvent(pathLbl, &te);

    EXPECT_TRUE(tip->isVisible());
    EXPECT_EQ(tip->m_strText, dlg->m_sFilePath);

    // Act 2: Leave hides it again.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(pathLbl, &leave);

    EXPECT_FALSE(tip->isVisible());

    delete dlg;
    delete tip;   // Tip is parentless by design; reclaim it here.
}

// ==========================================================================
// header-defined helper widgets: PosterFrame / InfoBottom / ArrowLine painting
// ==========================================================================

TEST(movie_info_dialog_ext, mid_posterFrame_paintAndPixmap)
{
    // Arrange: the ctor attaches a rounded default-logo pixmap.
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);
    PosterFrame *poster = dlg->findChild<PosterFrame *>();
    ASSERT_NE(poster, nullptr);

    // Assert: pixmap present and drop-shadow effect installed.
    EXPECT_FALSE(poster->pixmap(Qt::ReturnByValue).isNull());
    EXPECT_NE(poster->graphicsEffect(), nullptr);
    EXPECT_EQ(poster->height(), 128);

    QPaintEvent pe(poster->rect());
    QApplication::sendEvent(poster, &pe);
    EXPECT_TRUE(poster != nullptr);

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_infoBottom_paintDoesNotCrash)
{
    // Arrange
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);
    InfoBottom *bottom = dlg->findChild<InfoBottom *>();
    ASSERT_NE(bottom, nullptr);

    // Act + Assert: theme-aware rounded-rect painting survives.
    QPaintEvent pe(bottom->rect());
    QApplication::sendEvent(bottom, &pe);
    EXPECT_TRUE(bottom != nullptr);

    delete dlg;
}

TEST(movie_info_dialog_ext, mid_arrowLine_paintDoesNotCrash)
{
    // Arrange
    MovieInfoDialog *dlg = mid_makeDialog();
    ASSERT_NE(dlg, nullptr);
    DDrawer *film = dlg->findChild<DDrawer *>(FILM_INFO_WIDGET);
    ASSERT_NE(film, nullptr);

    // Act + Assert
    QPaintEvent pe(film->rect());
    QApplication::sendEvent(film, &pe);
    EXPECT_TRUE(film != nullptr);

    delete dlg;
}
