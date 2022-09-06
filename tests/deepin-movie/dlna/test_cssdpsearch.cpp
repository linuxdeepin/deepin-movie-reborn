// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stub/stub.h"
#include <gtest/gtest.h>
#include "cssdpsearch.h"
/*******************************函数打桩************************************/

class UT_CSSDPSearch : public QObject, public ::testing::Test
{
public:
    UT_CSSDPSearch(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        m_tester = new CSSDPSearch();
    }

    virtual void TearDown()
    {
        delete m_tester;
    }

protected:
    CSSDPSearch *m_tester;
};

TEST_F(UT_CSSDPSearch, initTest)
{

}

TEST_F(UT_CSSDPSearch, readMsg)
{
    m_tester->readMsg();
}

TEST_F(UT_CSSDPSearch, SsdpSearch)
{
    m_tester->SsdpSearch();
}

TEST_F(UT_CSSDPSearch, showDlnaCastAddr)
{
    QByteArray replyData = "AVTransport:\nLOCATION: http://10.8.13.142:56707/description.xml\r";
    m_tester->showDlnaCastAddr(replyData);
}
