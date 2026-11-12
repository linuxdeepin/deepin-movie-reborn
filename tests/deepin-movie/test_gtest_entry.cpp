// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Pure gtest entry point (no QTest::qExec wrapper): gtest CLI arguments such as
// --gtest_filter / --gtest_output reach RUN_ALL_TESTS() untouched, and the
// process exit code reflects the real test result. Qt event-simulation helpers
// (QTest::qWait, QSignalSpy, ...) used inside test bodies are plain runtime
// utilities and keep working under the gtest runner.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include "application.h"
#include "mainwindow.h"
#include "movie_configuration.h"
#include "player_widget.h"
#include "backends/mpv/mpv_proxy.h"

using namespace dmr;

#include <QLineEdit>

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#ifndef __mips__
#include <sanitizer/asan_interface.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>

// Force flush coverage data on crash so .gcda files are preserved (mirrors the
// platform test harness). deepin-movie-test has flaky cross-suite crashes; a
// crash would otherwise skip gcov's atexit flush and drop ALL coverage.
extern "C" void __gcov_dump(void);
static void crashHandler(int sig) {
    fprintf(stderr, "\n=== Crash detected (signal %d), flushing coverage data ===\n", sig);
    __gcov_dump();
    _exit(128 + sig);
}
static void setupCrashHandler() {
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);
}

int main(int argc, char *argv[])
{
    // Self-wrap in Xvfb when launched bare (no runner, no explicit override):
    // mpv's video render path needs a real or virtual X server -- Qt offscreen
    // segfaults in mpv's GL render on visible windows, while the desktop
    // session must not receive our windows at all. test-prj-running.sh wraps
    // us with DMR_UT_XVFB=1 already, so this only kicks in for manual runs.
    // If xvfb-run is unavailable we fall through to the offscreen default.
    if (qEnvironmentVariableIsEmpty("DMR_UT_XVFB")
        && qEnvironmentVariableIsEmpty("DMR_UT_FORCE_DISPLAY")
        && access("/usr/bin/xvfb-run", X_OK) == 0) {
        char exePath[PATH_MAX];
        if (realpath(argv[0], exePath)) {
            std::vector<char *> childArgs;
            childArgs.push_back(const_cast<char *>("xvfb-run"));
            childArgs.push_back(const_cast<char *>("-a"));
            childArgs.push_back(const_cast<char *>("--server-args=-screen 0 1600x1200x24"));
            childArgs.push_back(exePath);
            for (int i = 1; i < argc; ++i)
                childArgs.push_back(argv[i]);
            childArgs.push_back(nullptr);
            // Mark the child so it does not wrap itself again (and keeps its
            // configured platform instead of falling back to offscreen).
            setenv("DMR_UT_XVFB", "1", 1);
            execv("/usr/bin/xvfb-run", childArgs.data());
            // execv only returns on failure; continue with the offscreen path.
        }
    }

    setupCrashHandler();

    // Platform selection: tests/lib run in one of three modes.
    //  - default:            force the Qt "offscreen" plugin so windows never
    //                        pop up on the desktop session. NOTE: mpv's GL
    //                        render path segfaults there on *visible* windows,
    //                        so suites with real playback rendering must run
    //                        under xvfb instead.
    //  - DMR_UT_XVFB=1:      the runner wraps the process in xvfb-run; keep
    //                        the (X11) platform untouched for full mpv
    //                        rendering fidelity without touching the desktop.
    //  - DMR_UT_FORCE_DISPLAY=1: keep whatever platform is configured (UI
    //                        debugging against the live session).
    if (qEnvironmentVariableIsEmpty("DMR_UT_XVFB") && qEnvironmentVariableIsEmpty("DMR_UT_FORCE_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    Application app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);

    setlocale(LC_NUMERIC, "C");

    // Drop the persisted playlist from previous sessions: stale entries (files
    // that no longer exist, or paths from a previous checkout) make playByName
    // take the "playlist not empty" path instead of starting the pending play
    // request, which keeps the engine in Idle forever and hangs playback tests.
    {
        QDir appConfig(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                       + "/" + QCoreApplication::applicationName());
        QFile::remove(appConfig.filePath("playlist"));
    }

    MainWindow *pMainWindow = new MainWindow();
    // The test build does not define _LIBDMR_, so MpvProxy's constructor skips
    // firstInit() (mpv handle + GL widget creation). Without it mpv never
    // processes any loadfile and the engine state machine stays in Idle
    // forever, hanging every test that waits for real playback (shortCutPlay,
    // movieInfoDialog). Initialize it explicitly; firstInit() is unconditionally
    // compiled and safe outside the _LIBDMR_ build.
    if (auto *proxy = dynamic_cast<MpvProxy *>(pMainWindow->engine()->getMpvProxy())) {
        proxy->firstInit();
    }
    MovieConfiguration::get().init();
    app.setMainWindow(pMainWindow);

    // Default XML report path (still overridable via --gtest_output=...).
    testing::GTEST_FLAG(output) = "xml:./report/report_deepin-movie-test.xml";
    testing::InitGoogleTest(&argc, argv);

    // Filter precedence: a CLI --gtest_filter (already parsed by InitGoogleTest)
    // wins when given; the DMR_GTEST_FILTER env var is kept for the legacy
    // batched runners; the exclusion list below is ALWAYS appended so the known
    // crashers/hangers stay excluded in every mode.
    std::string fl = testing::GTEST_FLAG(filter);
    if (const char *envfl = getenv("DMR_GTEST_FILTER")) {
        fl = envfl;
    }
    // boost_dm_wid dereferences shared-toolbox members that are null in the test
    // harness and crashes; running it before the base suites aborts everything,
    // and reordering it to link-last destabilises the base link order. The whole
    // suite is excluded; the crashHandler still flushes coverage on any other crash.
    // MainWindow.shortCutPlay starts real playback of a media file; in UT
    // environments without the /data/source media assets the player loop
    // never settles and the case hangs indefinitely (no crash, so the
    // crashHandler cannot help and the whole single-process run stalls).
    // dmrsettings_ext.decodeAndEffectHandlers flips decode modes and waits for
    // the settings-changed cascade; on UT hosts without a fully working GPU/DRM
    // probe path the cascade never settles and the case hangs indefinitely.
    // Excluded so the single full run can complete; re-enable each entry once
    // its underlying defect is fixed.
    fl += "-boost_dm_wid.*"
          ":MainWindow.shortCutPlay"
          ":dmrsettings_ext.decodeAndEffectHandlers";
    testing::GTEST_FLAG(filter) = fl;
    int ret = RUN_ALL_TESTS();
#if !defined(__mips__) && defined(__SANITIZE_ADDRESS__)
    __sanitizer_set_report_path("asan.log");
#endif

    // Flush coverage counters before _exit (skips the Qt static teardown, which
    // is not crash-safe here, same as the former cleanupTestCase _exit(0)).
    // Unlike the old QTest wrapper this propagates the real gtest result as the
    // process exit code, so runners can retry on genuine failures.
    __gcov_dump();
    _exit(ret == 0 ? 0 : 1);
}
