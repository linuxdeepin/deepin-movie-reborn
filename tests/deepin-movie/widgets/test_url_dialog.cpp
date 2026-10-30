// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Google Test cases for src/widgets/url_dialog.cpp (dmr::UrlDialog) and
// src/widgets/dmr_lineedit.cpp (dmr::LineEdit) — UT-movie-t14.
//
// Scope agreed for this task: the existing platform suite
// (tests/deepin-movie-platform/widgets/test_misc_widgets_ext.cpp) already
// covers UrlDialog construction, slotTextchanged, url() empty/local/
// unsupported/supported/onlyScheme, showEvent focus, default button and
// fixed size, plus LineEdit construct/clear basics — those cases stay
// untouched. This file fills the agreed gaps with real assertions:
//   * url() returning a parsed QUrl for a genuinely playable address
//     (preconditioned on Settings::iscommonPlayableProtocol, no network)
//   * a scheme sweep asserting the empty-verdict rule over every common
//     protocol family
//   * OK/Cancel button clicks -> finished(int) + QDialog::result()
//   * OK stays silent while disabled (empty input)
//   * LineEdit ctor three-state clear icons from compiled resources and
//     the clear action wiring (clears text, toggles by text emptiness)
//
// Conventions (mirroring the existing suites):
//   * Google Test only, no main() here; suite names "urldialog_t14" and
//     "lineedit_t14" are unique inside deepin-movie-test.
//   * `#define protected public` before project includes, `#undef` after.
//   * No real network: the playable-URL case is gated on a protocol the
//     Settings whitelist already considers common; nothing is fetched.

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFile>
#include <QIcon>
#include <QFocusEvent>
#include <QShowEvent>

#define protected public
#define private public
#include "src/widgets/url_dialog.h"
#include "src/widgets/dmr_lineedit.h"
#include "src/common/dmr_settings.h"
#undef private
#undef protected

using namespace dmr;

namespace {

struct DialogFixture {
    UrlDialog dlg;

    DialogFixture()
    {
        dlg.resize(300, 140);
        dlg.show();
        QTest::qWaitForWindowExposed(&dlg);
    }
};

} // namespace

// ------------------------------------------------------------------- url()

TEST(urldialog_t14, urlReturnsParsedQUrlForPlayableInput)
{
    // url() forwards through Settings::iscommonPlayableProtocol; this case
    // locks the "returns a parsed absolute QUrl" contract for a scheme that
    // the whitelist already treats as common (no network traffic happens —
    // the address is never fetched, only parsed).
    const QString candidate = QStringLiteral("http://1.2.3.4/example.mp4");
    if (!Settings::get().iscommonPlayableProtocol(QStringLiteral("http"))) {
        GTEST_SKIP() << "scheme not on the common-playable whitelist in this env";
    }

    DialogFixture fx;
    fx.dlg.m_lineEdit->setText(candidate);

    const QUrl got = fx.dlg.url();
    EXPECT_EQ(got, QUrl(candidate));
    EXPECT_TRUE(got.isValid());
    EXPECT_EQ(got.toString(), candidate);
}

TEST(urldialog_t14, urlVerdictTracksCommonPlayableProtocols)
{
    // The empty-verdict rule must hold for every scheme family: whatever
    // iscommonPlayableProtocol decides, an EMPTY result equals NOT playable.
    static const char *kSchemes[] = {
        "http", "https", "ftp", "rtsp", "rtmp", "mms", "hdh", "udp", "rtp",
        "file", "smb", "sftp", "dlna", "mtp", "gopher", "x-media"
    };

    DialogFixture fx;
    for (const char *schemeName : kSchemes) {
        const QString scheme = QString::fromLatin1(schemeName);
        const QString url = QStringLiteral("%1://1.2.3.4/f.mp4").arg(scheme);
        fx.dlg.m_lineEdit->setText(url);
        const QUrl got = fx.dlg.url();

        const bool empty = got.isEmpty();
        const bool playable = Settings::get().iscommonPlayableProtocol(scheme);
        EXPECT_EQ(empty, !playable)
                << "scheme=" << scheme.toStdString() << " empty=" << empty
                << " playable=" << playable;
    }
}

// ---------------------------------------------------------------- buttons

TEST(urldialog_t14, okClickEmitsFinishedAcceptedWithTypedUrl)
{
    DialogFixture fx;
    QSignalSpy finished(&fx.dlg, &QDialog::finished);

    fx.dlg.m_lineEdit->setText("http://example.com/v.mp4");
    fx.dlg.getButton(1)->click(); // OK

    ASSERT_EQ(finished.count(), 1);
    EXPECT_EQ(finished.first().at(0).toInt(), int(QDialog::Accepted));
    EXPECT_EQ(fx.dlg.result(), int(QDialog::Accepted));
}

TEST(urldialog_t14, cancelButtonEmitsFinishedRejected)
{
    DialogFixture fx;
    QSignalSpy finished(&fx.dlg, &QDialog::finished);

    fx.dlg.m_lineEdit->setText("http://example.com/v.mp4"); // even with input
    fx.dlg.getButton(0)->click(); // Cancel

    ASSERT_EQ(finished.count(), 1);
    EXPECT_EQ(finished.first().at(0).toInt(), int(QDialog::Rejected));
    EXPECT_EQ(fx.dlg.result(), int(QDialog::Rejected));
}

TEST(urldialog_t14, okClickWhileDisabledIsANoOp)
{
    DialogFixture fx; // empty line edit -> OK stays disabled
    QSignalSpy finished(&fx.dlg, &QDialog::finished);

    QAbstractButton *ok = fx.dlg.getButton(1);
    ASSERT_NE(ok, nullptr);
    EXPECT_FALSE(ok->isEnabled());

    ok->click(); // disabled buttons must not emit clicked()

    EXPECT_EQ(finished.count(), 0);
    EXPECT_NE(fx.dlg.result(), int(QDialog::Accepted));
}

// ------------------------------------------------------------- showEvent

TEST(urldialog_t14, showEventPutsKeyboardFocusOnLineEdit)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    DialogFixture fx;

    EXPECT_EQ(fx.dlg.focusWidget(), fx.dlg.m_lineEdit);
}

// --------------------------------------------------------------- LineEdit

TEST(lineedit_t14, ctorWiresThreeStateClearIcons)
{
    QWidget parent;
    LineEdit le(&parent);

    ASSERT_NE(le.m_pClearAct, nullptr);

    // three distinct SVG files are baked into the compiled resources
    EXPECT_TRUE(QFile::exists(":/resources/icons/input_clear_normal.svg"));
    EXPECT_TRUE(QFile::exists(":/resources/icons/input_clear_press.svg"));
    EXPECT_TRUE(QFile::exists(":/resources/icons/input_clear_hover.svg"));
    const QIcon icon = le.m_pClearAct->icon();
    EXPECT_FALSE(icon.isNull());
    // the icon actually rasterises (svg icons report empty availableSizes)
    EXPECT_FALSE(icon.pixmap(16, 16).isNull());
}

TEST(lineedit_t14, clearActionTriggeredClearsText)
{
    QWidget parent;
    LineEdit le(&parent);
    le.setText("https://example.com");

    le.m_pClearAct->trigger();

    EXPECT_TRUE(le.text().isEmpty());
    EXPECT_FALSE(le.isClearButtonEnabled()); // slotTextChanged(empty) disables it
}

TEST(lineedit_t14, clearButtonTogglesWithTextEmptiness)
{
    QWidget parent;
    LineEdit le(&parent);
    EXPECT_FALSE(le.isClearButtonEnabled()); // ctor default (empty text)

    le.setText("abc"); // textChanged -> slotTextChanged
    EXPECT_TRUE(le.isClearButtonEnabled());

    le.m_pClearAct->trigger();
    EXPECT_FALSE(le.isClearButtonEnabled());
}

TEST(lineedit_t14, showEventAdjustsHeight)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    LineEdit le(&parent);
    le.resize(200, 40);

    QShowEvent se;
    QApplication::sendEvent(&le, &se);

    // showEvent clamps the height to the 36px design maximum
    EXPECT_EQ(le.height(), 36);
}
