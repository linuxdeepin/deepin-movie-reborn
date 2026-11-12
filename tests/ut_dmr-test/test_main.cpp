// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Pure gtest entry point (no QTest::qExec wrapper): gtest CLI arguments such as
// --gtest_filter / --gtest_output reach RUN_ALL_TESTS() untouched, and the
// process exit code reflects the real test result.

#include <QCoreApplication>
#include <QUrl>

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#ifndef __mips__
#include <sanitizer/asan_interface.h>
#endif

#include "window.h"
#include "application.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>

// Force flush coverage data on crash so .gcda files are preserved (mirrors the
// main test harnesses).
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

    // Force Qt "offscreen" by default (no windows on the desktop session).
    // DMR_UT_XVFB=1 (runner wraps us in xvfb-run) or DMR_UT_FORCE_DISPLAY=1
    // (UI debugging) keep the configured platform untouched.
    if (qEnvironmentVariableIsEmpty("DMR_UT_XVFB") && qEnvironmentVariableIsEmpty("DMR_UT_FORCE_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    Application app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);

    setlocale(LC_NUMERIC, "C");

    Window w;
    w.play(QUrl(DMR_TEST_ASSETS_DIR "/demo.mp4"));

    app.setMainWindow(&w);

    // Default XML report path (still overridable via --gtest_output=...).
    testing::GTEST_FLAG(output) = "xml:./report/report_dmr-test.xml";
    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
#if !defined(__mips__) && defined(__SANITIZE_ADDRESS__)
    __sanitizer_set_report_path("asan.log");
#endif

    // Flush coverage counters before _exit (skips the Qt static teardown, which
    // is not crash-safe here). Unlike the old QTest wrapper this propagates the
    // real gtest result as the process exit code, so runners can retry on
    // genuine failures.
    __gcov_dump();
    _exit(ret == 0 ? 0 : 1);
}
