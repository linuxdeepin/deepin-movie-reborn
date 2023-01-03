/*
* Copyright (C) 2019 ~ 2020 UnionTech Software Technology Co.,Ltd
*
* Author:     zhanghao<zhanghao@uniontech.com>
* Maintainer: zhanghao<zhanghao@uniontech.com>
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
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
    exit(0);
}

void QTestMain::testGTest()
{
    testing::GTEST_FLAG(output) = "xml:./report/report_deepin-movie-test.xml";
    testing::InitGoogleTest(&m_argc,m_argv);
    int ret = RUN_ALL_TESTS();
#ifndef __mips__
    __sanitizer_set_report_path("asan.log");
#endif
    Q_UNUSED(ret)

//    exit(0);
}

int main(int argc, char *argv[])
{
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
