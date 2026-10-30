// Copyright (C) 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests for the remaining classes defined inside
// src/widgets/toolbox_proxy.cpp — viewProgBarLoad (thumbnail QThread),
// ThumbnailPreview, SliderTime, TooltipHandler — plus the ToolboxProxy
// methods these widgets drive (mouseMoveEvent, updatePreviewTime,
// updateMircastTime, initToolTip, slotUpdateThumbnailTimeOut worker path).
//
// Design notes:
//   * The cpp-local classes need complete types: toolbox_shadow_classes.h
//     carries a verbatim copy of their definitions (see that header).
//   * The thumbnailer library functions are REAL when libffmpegthumbnailer
//     is resolvable; the only external state they touch on an empty playlist
//     is a failed open — no external process is spawned because
//     PlayerEngine::duration is stubbed above the ffprobe threshold (300s).
//   * The fake-thumbnail case swaps the resolved generate function pointer on
//     the thread object (accessible through the private->public parse hack)
//     with one that feeds a valid PNG buffer, so the mutex/addpmList/emit
//     tail of loadViewProgBar is covered fully offline.
//   * QThread::run is never start()ed except in the production-shaped
//     slotUpdateThumbnailTimeOut case, which joins the worker before the
//     fixture tears the proxy down.
//   * No mpv backend is exercised; the engine stays Idle.

#include <QtTest>
#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QHelpEvent>
#include <QBuffer>
#include <QMutex>
#include <QTemporaryFile>
#include <QStackedWidget>
#include <DSlider>
#include <DLabel>

#define protected public
#define private public
#include "src/widgets/toolbox_proxy.h"
// Shadow definitions: ViewProgBar / ThumbnailPreview / SliderTime /
// TooltipHandler are defined inside toolbox_proxy.cpp and only
// forward-declared in the header; the shadow copy gives complete types.
#include "toolbox_shadow_classes.h"
#undef protected
#undef private

#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "application.h"
#include "stub/stub.h"

using namespace dmr;

#ifndef POPUP_DURATION
#define POPUP_DURATION 350   // keep in sync with toolbox_proxy.cpp
#endif

// ---------------------------------------------------------------------------
// PlayerEngine::duration capture stub — loadViewProgBar branches on
// duration() < 300 to spawn ffprobe/ffmpeg. Tests must not depend on external
// processes, so every thumbnail-path test pins duration above 300.
// ---------------------------------------------------------------------------
namespace {

static qint64 vpl_stubbedDuration = 0;

static qint64 vpl_duration_stub(void * /*self*/)
{
    return vpl_stubbedDuration;
}

typedef qint64 (*vpl_duration_fptr)(void *);

static Stub vpl_durationStub;

static void vpl_pinDuration(qint64 secs)
{
    vpl_durationStub.set((vpl_duration_fptr)&PlayerEngine::duration,
                         vpl_duration_stub);
    vpl_stubbedDuration = secs;
}

static void vpl_restoreDuration()
{
    vpl_durationStub.reset((vpl_duration_fptr)&PlayerEngine::duration);
}

// PlaylistModel::currentInfo() Q_ASSERTs on an empty list; every
// loadViewProgBar-shaped test reads the current item's url, so seed one
// harmless (existing, non-media) file into the engine playlist first.
// Fake generate_thumbnail_to_buffer: serves a small valid PNG so the
// QImage::fromData in loadViewProgBar succeeds and the full tail
// (destroy_image_data -> mutex -> addpmList -> sigFinishiLoad) is executed.
static int vpl_fake_generate(video_thumbnailer * /*thumbnailer*/,
                             const char * /*movie_filename*/,
                             image_data *generated)
{
    static QByteArray png;
    if (png.isEmpty()) {
        QImage img(2, 2, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
    }
    generated->image_data_ptr = reinterpret_cast<uint8_t *>(png.data());
    generated->image_data_size = png.size();
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Fixture — same shape as ToolboxProxyExtTest: fresh ToolboxProxy per test,
// shared MainWindow, playlist wired synchronously.
// ---------------------------------------------------------------------------
class ToolboxWidgetsExtTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_w = dApp->getMainWindow();
        ASSERT_NE(m_w, nullptr);
        ASSERT_NE(m_w->engine(), nullptr);

        m_tb = new ToolboxProxy(m_w, m_w->engine());
        if (!m_w->playlist()) {
            m_w->loadPlayList();
        }
        ASSERT_NE(m_w->playlist(), nullptr);
        // keep the shared engine playlist clean: other suites may have seeded
        // entries and several of these paths read playlist().current()
        m_w->engine()->playlist().clear();
        m_tb->setPlaylist(m_w->playlist());
        ASSERT_NE(m_tb->m_pPlaylist, nullptr);
        m_tb->show();
        QTest::qWait(30);
    }

    void TearDown() override
    {
        if (m_fake) {
            m_fake->close();
            QFile::remove(m_fake->fileName());
            delete m_fake;
            m_fake = nullptr;
        }
        // join a still-running worker thread before the proxy dies
        if (m_tb && m_tb->m_pWorker && m_tb->m_pWorker->isRunning()) {
            m_tb->m_pWorker->requestInterruption();
            for (int i = 0; i < 100 && m_tb->m_pWorker->isRunning(); ++i) {
                QTest::qWait(50);
            }
        }
        delete m_tb;
        m_tb = nullptr;
        if (m_w && m_w->engine()) {
            m_w->engine()->playlist().clear();
        }
    }

    MainWindow *m_w = nullptr;
    ToolboxProxy *m_tb = nullptr;
    QTemporaryFile *m_fake = nullptr;

    // loadViewProgBar reads playlist().currentInfo().url — the model
    // Q_ASSERTs on an empty list. isPlayableFile filters by extension, so a
    // zero-byte *.mp4 passes the filter and the thumbnailer fails on it fast.
    void seedFakeMedia()
    {
        if (!m_fake) {
            m_fake = new QTemporaryFile("/tmp/ut-vpl-XXXXXX.mp4");
            m_fake->setAutoRemove(false);
            ASSERT_TRUE(m_fake->open());
        }
        auto &model = m_w->engine()->playlist();
        if (model.count() == 0) {
            // direct injection: appendAsync spawns probing threads that
            // never settle on a zero-byte file inside the test binary
            PlayItemInfo pif;
            pif.valid = true;
            pif.loaded = true;
            pif.url = QUrl::fromLocalFile(m_fake->fileName());
            pif.info = QFileInfo(m_fake->fileName());
            model._infos.append(pif);
        }
        ASSERT_GT(model.count(), 0);
    }
};

// ---------------------------------------------------------------------------
// ThumbnailPreview
// ---------------------------------------------------------------------------
TEST_F(ToolboxWidgetsExtTest, ThumbnailPreview_ctor_InitializesState)
{
    // Arrange
    // Act
    ThumbnailPreview tp;

    // Assert
    EXPECT_NE(tp.m_shadow_effect, nullptr);
    EXPECT_EQ(tp.m_thumbnailFixed, 106);
    EXPECT_EQ(tp.objectName(), QString("ThumbnailPreview"));
    EXPECT_EQ(tp.m_bIsWM, DWindowManagerHelper::instance()->hasBlurWindow());
    EXPECT_EQ(tp.width(), 0);   // resize(0,0) until a preview arrives
}

TEST_F(ToolboxWidgetsExtTest, updateWithPreview_WidePixmap_ScalesToFixed)
{
    // Arrange
    ThumbnailPreview tp;
    const int offect = tp.m_bIsWM ? 2 : 0;

    // Act
    tp.updateWithPreview(QPixmap(120, 60), 100, 0);

    // width > height branch: target size (106, 106*60/120=53)
    // Assert
    EXPECT_EQ(tp.width(), 106 + offect);
    EXPECT_EQ(tp.height(), 53 + offect);
    EXPECT_FALSE(tp.m_thumbImg.isNull());
}

TEST_F(ToolboxWidgetsExtTest, updateWithPreview_TallPixmap_ScalesToFixed)
{
    // Arrange
    ThumbnailPreview tp;
    const int offect = tp.m_bIsWM ? 2 : 0;

    // Act
    tp.updateWithPreview(QPixmap(60, 120), 100, 0);

    // width <= height branch: target size (106*60/120=53, 106)
    // Assert
    EXPECT_EQ(tp.width(), 53 + offect);
    EXPECT_EQ(tp.height(), 106 + offect);
    EXPECT_FALSE(tp.m_thumbImg.isNull());
}

TEST_F(ToolboxWidgetsExtTest, updateWithPreview_EmptyPixmap_NoLayoutChange)
{
    // Arrange
    ThumbnailPreview tp;

    // Act
    tp.updateWithPreview(QPixmap(), 100, 0);

    // Assert
    EXPECT_EQ(tp.width(), 0);
    EXPECT_EQ(tp.height(), 0);
    EXPECT_TRUE(tp.m_thumbImg.isNull());
}

TEST_F(ToolboxWidgetsExtTest, updateWithPreview_Pos_ValidGeometryShows)
{
    // Arrange
    ThumbnailPreview tp;
    // Act
    tp.updateWithPreview(QPixmap(120, 60), 100, 0);   // give it a real size
    // Assert
    ASSERT_GT(tp.width(), 0);

    tp.updateWithPreview(QPoint(100, 100));

    EXPECT_EQ(tp.x(), 100 - tp.width() / 2);
    EXPECT_FALSE(tp.isHidden());   // geometry().isValid() -> show()
}

TEST_F(ToolboxWidgetsExtTest, updateWithPreview_Pos_InvalidGeometryStaysHidden)
{
    // Arrange
    ThumbnailPreview tp;   // 0x0 -> geometry invalid

    // Act
    tp.updateWithPreview(QPoint(100, 100));

    // Assert
    EXPECT_EQ(tp.x(), 100);
    EXPECT_TRUE(tp.isHidden());    // show() never called
}

TEST_F(ToolboxWidgetsExtTest, slotWMChanged_BlurWindow_RefreshesState)
{
    // Arrange
    ThumbnailPreview tp;

    // Act
    tp.slotWMChanged();

    // Assert
    EXPECT_EQ(tp.m_bIsWM, DWindowManagerHelper::instance()->hasBlurWindow());
    EXPECT_EQ(tp.m_thumbnailFixed, 106);
}

TEST_F(ToolboxWidgetsExtTest, leaveEvent_MouseLeave_EmitsLeavePreview)
{
    // Arrange
    ThumbnailPreview tp;
    QSignalSpy spy(&tp, &ThumbnailPreview::leavePreview);

    QEvent leave(QEvent::Leave);

    // Act
    QApplication::sendEvent(&tp, &leave);
    QApplication::sendEvent(&tp, &leave);

    // Assert: every Leave event re-emits leavePreview; widget stays non-visible
    EXPECT_EQ(spy.count(), 2);
    EXPECT_FALSE(tp.isVisible());
}

TEST_F(ToolboxWidgetsExtTest, paintEvent_IdleEngine_RendersWithoutCrash)
{
    // Arrange
    // Act
    ThumbnailPreview tp;
    tp.updateWithPreview(QPixmap(120, 60), 100, 0);
    // Assert
    ASSERT_GT(tp.width(), 0);

    QPixmap canvas(160, 160);
    canvas.fill(Qt::darkGray);
    tp.render(&canvas, QPoint(10, 10));

    // Assert: paintEvent ran on a real device and left the canvas usable
    EXPECT_FALSE(canvas.isNull());
    EXPECT_EQ(canvas.size(), QSize(160, 160));
}

// ---------------------------------------------------------------------------
// SliderTime
// ---------------------------------------------------------------------------
TEST_F(ToolboxWidgetsExtTest, setTime_100Seconds_UpdatesLabel)
{
    // Arrange
    SliderTime st;

    // Act
    st.setTime("00:01:40");

    // Assert
    EXPECT_EQ(st.m_pTime->text(), QString("00:01:40"));
    EXPECT_GT(st.m_pTime->width(), 0);
}

// ---------------------------------------------------------------------------
// viewProgBarLoad
// ---------------------------------------------------------------------------
TEST_F(ToolboxWidgetsExtTest, viewProgBarLoad_CtorAndDtor_WireMembers)
{
    // Arrange
    {
        viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);

    // Act
        EXPECT_EQ(vpl.m_pEngine, m_w->engine());
    // Assert
        EXPECT_EQ(vpl.m_pProgBar, m_tb->m_pProgBar);
        EXPECT_EQ(vpl.m_pParent, m_tb);
        EXPECT_NE(vpl.m_seekTime, nullptr);
        // the shared library resolves in this environment; if it ever does
        // not, initThumb leaves the pointers null by design (still no crash)
    }
    SUCCEED();   // destructor ran (frees m_seekTime, destroys thumbnailer)
}

TEST_F(ToolboxWidgetsExtTest, initMember_SecondCall_ResetsPointers)
{
    // Arrange
    viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);

    // Act
    vpl.initMember();

    // Assert
    EXPECT_EQ(vpl.m_pEngine, nullptr);
    EXPECT_EQ(vpl.m_pParent, nullptr);
    EXPECT_EQ(vpl.m_pProgBar, nullptr);
    EXPECT_EQ(vpl.m_pListPixmapMutex, nullptr);
}

TEST_F(ToolboxWidgetsExtTest, setListPixmapMutex_NewMutex_Stores)
{
    // Arrange
    viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);
    QMutex mutex;

    // Act
    vpl.setListPixmapMutex(&mutex);

    // Assert
    EXPECT_EQ(vpl.m_pListPixmapMutex, &mutex);
    EXPECT_EQ(vpl.m_pParent, m_tb);
}

TEST_F(ToolboxWidgetsExtTest, initThumb_FirstCall_ResolvesThumbnailer)
{
    // Arrange
    viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);

    // Act
    vpl.initThumb();

    if (vpl.m_mvideo_thumbnailer == nullptr) {
        GTEST_SKIP() << "libffmpegthumbnailer not resolvable in this env";
    }

    // Assert: the thumbnailer handle and generate-function got resolved
    EXPECT_NE(vpl.m_video_thumbnailer, nullptr);
    EXPECT_NE(vpl.m_mvideo_thumbnailer, nullptr);
}

TEST_F(ToolboxWidgetsExtTest, run_InterruptRequested_ReturnsBeforeGenerate)
{
    // Arrange
        seedFakeMedia();
    viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);
    QMutex mutex;
    vpl.setListPixmapMutex(&mutex);
    m_tb->m_pProgBar->resize(200, 40);   // num = 200/41 = 4 > 0
    vpl_pinDuration(1000);               // > 300: no ffprobe/ffmpeg spawn
    QSignalSpy spy(&vpl, &viewProgBarLoad::sigFinishiLoad);

    vpl.requestInterruption();
    // Act
    vpl.run();   // synchronous, never start()ed

    // interrupted before the first generate: no emit, image data kept
    // Assert
    EXPECT_EQ(spy.count(), 0);
    EXPECT_NE(vpl.m_image_data, nullptr);
    vpl_restoreDuration();
}

TEST_F(ToolboxWidgetsExtTest, run_FakeThumbnail_FullTail)
{
    // Arrange
        seedFakeMedia();
    viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);
    QMutex mutex;
    vpl.setListPixmapMutex(&mutex);
    m_tb->m_pProgBar->resize(200, 40);   // num = 4
    vpl_pinDuration(1000);
    vpl.m_mvideo_thumbnailer_generate_thumbnail_to_buffer = &vpl_fake_generate;
    QSignalSpy spy(&vpl, &viewProgBarLoad::sigFinishiLoad);

    // Act
    vpl.run();

    // full tail executed: image data destroyed, pixmap list handed over,
    // completion signal emitted with the parent size
    // Assert
    EXPECT_EQ(vpl.m_image_data, nullptr);
    EXPECT_EQ(m_tb->m_pmList.size(), 4);
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().first().toSize(), m_tb->size());
    vpl_restoreDuration();
}

TEST_F(ToolboxWidgetsExtTest, run_GenerateFails_EarlyReturn)
{
    // Arrange
        seedFakeMedia();
    viewProgBarLoad vpl(m_w->engine(), m_tb->m_pProgBar, m_tb);
    QMutex mutex;
    vpl.setListPixmapMutex(&mutex);
    m_tb->m_pProgBar->resize(200, 40);   // num = 4 > 0
    vpl_pinDuration(1000);
    QSignalSpy spy(&vpl, &viewProgBarLoad::sigFinishiLoad);

    // Act
    vpl.run();   // real generate() on an empty playlist file fails fast

    // invalid image -> return before the mutex/addpmList/emit tail
    // Assert
    EXPECT_EQ(spy.count(), 0);
    EXPECT_NE(vpl.m_image_data, nullptr);
    vpl_restoreDuration();
}

// ---------------------------------------------------------------------------
// ToolboxProxy — production-shaped worker path + direct dispatch methods
// ---------------------------------------------------------------------------
TEST_F(ToolboxWidgetsExtTest, slotUpdateThumbnailTimeOut_LongVideo_SpawnsWorkerAndJoins)
{
    // Arrange
        seedFakeMedia();
    vpl_pinDuration(1000);   // > 1s: real worker path; > 300: no ffprobe

    // Act
    m_tb->slotUpdateThumbnailTimeOut();

    // Assert
    ASSERT_NE(m_tb->m_pWorker, nullptr);
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), 1);

    // the 500ms singleShot starts the worker; qWait keeps the event loop
    // running so the lambda actually fires, and the interruption flag set by
    // the slot makes run() return before any generate call
    QTest::qWait(700);
    for (int i = 0; i < 100 && m_tb->m_pWorker->isRunning(); ++i) {
        QTest::qWait(50);
    }
    EXPECT_FALSE(m_tb->m_pWorker->isRunning());
    EXPECT_TRUE(m_tb->m_pWorker->isFinished());
    vpl_restoreDuration();
}

TEST_F(ToolboxWidgetsExtTest, mouseMoveEvent_OverButtons_HidesButtonTooltips)
{
    // Arrange
    // non-wayland env: setButtonTooltipHide -> ToolButton::hideToolTip, which
    // stops m_showTime and only touches the window when it is visible (real
    // tooltip show/hide is unstable under the offscreen platform, so verify
    // through the timer state instead)
    // Act
    ASSERT_FALSE(utils::check_wayland_env());

    // Qt6 ignores buttonless MouseMove events while WA_MouseTracking is off;
    // the production toolbox tracks the mouse for hover preview
    m_tb->setAttribute(Qt::WA_MouseTracking, true);

    m_tb->m_pListBtn->m_showTime.start(60000);
    m_tb->m_pFullScreenBtn->m_showTime.start(60000);
    QMouseEvent move(QEvent::MouseMove, QPointF(30, 10),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(m_tb, &move);

    // Assert
    EXPECT_FALSE(m_tb->m_pListBtn->m_showTime.isActive());
    EXPECT_FALSE(m_tb->m_pFullScreenBtn->m_showTime.isActive());

    // direct call runs the same hide path on already-stopped timers
    m_tb->m_pListBtn->m_showTime.start(60000);
    m_tb->setButtonTooltipHide();
    EXPECT_FALSE(m_tb->m_pListBtn->m_showTime.isActive());
}

TEST_F(ToolboxWidgetsExtTest, updatePreviewTime_NewPos_SetsTimeAndShowsPopup)
{
    // Arrange
    // Act
    m_tb->updatePreviewTime(100, QPoint(20, 30));

    // Assert
    EXPECT_EQ(m_tb->m_pPreviewTime->m_pTime->text(), QString("00:01:40"));
    EXPECT_TRUE(m_tb->m_pPreviewTime->isVisible());
}

TEST_F(ToolboxWidgetsExtTest, updateMircastTime_ProgBarMode_SetsSliderPosition)
{
    // Arrange
    m_tb->m_pProgBar_Widget->setCurrentIndex(1);
    ASSERT_TRUE(m_tb->m_pProgBar->signalsBlocked() == false);

    // Act
    m_tb->updateMircastTime(120);

    // DMRSlider clamps to its own range; the unblock-set path must land
    // the clamped position deterministically
    const int expected = qBound(m_tb->m_pProgBar->slider()->minimum(), 120,
                                m_tb->m_pProgBar->slider()->maximum());
    // Assert
    EXPECT_EQ(m_tb->m_pProgBar->slider()->value(), expected);
    EXPECT_EQ(m_tb->m_pProgBar->slider()->sliderPosition(), expected);
    EXPECT_FALSE(m_tb->m_pProgBar->signalsBlocked());
}

TEST_F(ToolboxWidgetsExtTest, updateMircastTime_ViewProgBarMode_SetsValue)
{
    // Arrange
    m_tb->m_pProgBar_Widget->setCurrentIndex(0);

    // Act
    m_tb->updateMircastTime(120);

    // Assert
    EXPECT_FALSE(m_tb->m_pViewProgBar->m_bIsBlockSignals);
    // setValue clamps to [startPoint, startPoint+viewLength] (0 width here)
    const int expected = qBound(m_tb->m_pViewProgBar->m_nStartPoint, 120,
                                m_tb->m_pViewProgBar->m_nStartPoint
                                    + m_tb->m_pViewProgBar->m_nViewLength);
    EXPECT_EQ(m_tb->m_pViewProgBar->getValue(), expected);
}

TEST_F(ToolboxWidgetsExtTest, initToolTip_WaylandEnv_CreatesTipsAndRoutesSignals)
{
    // Arrange
    // offscreen binaries take the TooltipHandler path in the ctor; the
    // play/prev/next tip lambdas are only installed on the wayland branch,
    // which USE_TEST exposes via set_wayland()
    utils::set_wayland(true);
    // Act
    m_tb->initToolTip();
    utils::set_wayland(false);

    // Assert
    ASSERT_NE(m_tb->m_pPlayBtnTip, nullptr);
    ASSERT_NE(m_tb->m_pPrevBtnTip, nullptr);
    ASSERT_NE(m_tb->m_pNextBtnTip, nullptr);
    ASSERT_NE(m_tb->m_pFullScreenBtnTip, nullptr);
    ASSERT_NE(m_tb->m_pListBtnTip, nullptr);
    ASSERT_NE(m_tb->m_pVolBtnTip, nullptr);
    ASSERT_NE(m_tb->m_pMircastBtnTip, nullptr);

    // entered lambdas: move/show/update the tip above each button
    emit m_tb->m_pListBtn->entered();
    emit m_tb->m_pFullScreenBtn->entered();
    emit m_tb->m_pPlayBtn->entered();
    emit m_tb->m_pPrevBtn->entered();
    emit m_tb->m_pNextBtn->entered();
    qInfo("PROBE playTip visible=%d hidden=%d waylandflag=%d",
          (int)m_tb->m_pPlayBtnTip->isVisible(), (int)m_tb->m_pPlayBtnTip->isHidden(),
          (int)utils::check_wayland_env());
    emit m_tb->m_pVolBtn->entered();
    emit m_tb->m_pMircastBtn->entered();
    QTest::qWait(30);

    // leaved lambdas: singleShot(0) hides the tip again
    emit m_tb->m_pListBtn->leaved();
    emit m_tb->m_pFullScreenBtn->leaved();
    emit m_tb->m_pPlayBtn->leaved();
    emit m_tb->m_pPrevBtn->leaved();
    emit m_tb->m_pNextBtn->leaved();
    emit m_tb->m_pVolBtn->leaved();
    emit m_tb->m_pMircastBtn->leaved();
    QTest::qWait(80);

    EXPECT_TRUE(m_tb->m_pListBtnTip->isHidden());
    EXPECT_TRUE(m_tb->m_pPlayBtnTip->isHidden());
    EXPECT_TRUE(m_tb->m_pVolBtnTip->isHidden());
    EXPECT_TRUE(m_tb->m_pMircastBtnTip->isHidden());
}
