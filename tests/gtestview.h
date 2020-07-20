#ifndef GTESTVIEW_H
#define GTESTVIEW_H

//#include <QTest>
#include <gtest/gtest.h>
#include "common/mainwindow.h"

class gtestview : public ::testing::Test
{
public:
    gtestview();
    virtual void SetUp()
    {
//        dmr::MainWindow mw;
//        mw.show();
    }

    virtual void TearDown()
    {
//        delete m_tester;
    }
protected:
//    MainWindow*   m_tester;
};

#endif // GTESTVIEW_H
