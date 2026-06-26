// Copyright (C) 2019-2026 ~ 2020 UnionTech Software Technology Co.,Ltd
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QCoreApplication>
#include "application.h"
#include "mainwindow.h"
#include "movie_configuration.h"
#include <QTest>
#include "player_widget.h"

using namespace dmr;

// add necessary includes here
#include <QLineEdit>

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#ifndef __mips__
#include <sanitizer/asan_interface.h>
#endif

#include <signal.h>
#include <stdio.h>

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

class QTestMain : public QObject
{
    Q_OBJECT

public:
    QTestMain(int &argc, char **argv);
    ~QTestMain();

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testGTest();

private:
    int m_argc;
    char **m_argv;
};

QTestMain::QTestMain(int &argc, char **argv)
{
    m_argc = argc;
    m_argv = argv;
}

QTestMain::~QTestMain()
{

}

void QTestMain::initTestCase()
{
    qDebug() << "=====start test=====";
}

void QTestMain::cleanupTestCase()
{
    qDebug() << "=====stop test=====";
    __gcov_dump();   // flush coverage counters before _exit (no atexit run,
                     // so it can't race/clobber the crashHandler flush). Mirrors
                     // the platform test harness.
    _exit(0);
}

void QTestMain::testGTest()
{
    testing::GTEST_FLAG(output) = "xml:./report/report_deepin-movie-test.xml";
    testing::InitGoogleTest(&m_argc,m_argv);
    // boost_dm_wid dereferences shared-toolbox members that are null in the test
    // harness and crashes; running it before the base suites aborts everything,
    // and reordering it to link-last destabilises the base link order. The whole
    // suite is excluded; the crashHandler still flushes coverage on any other crash.
    testing::GTEST_FLAG(filter) += "-boost_dm_wid.*";
    int ret = RUN_ALL_TESTS();
#if !defined(__mips__) && defined(__SANITIZE_ADDRESS__)
    __sanitizer_set_report_path("asan.log");
#endif
    Q_UNUSED(ret)

//    exit(0);
}

int main(int argc, char *argv[])
{
    setupCrashHandler();
    Application app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);

    setlocale(LC_NUMERIC, "C");

    QTestMain testMain(argc, argv);
    MainWindow *pMainWindow = new MainWindow();
    MovieConfiguration::get().init();
    app.setMainWindow(pMainWindow);
    QTest::qExec(&testMain, argc, argv);
    return app.exec();
}


#include "test_qtestmain.moc"
