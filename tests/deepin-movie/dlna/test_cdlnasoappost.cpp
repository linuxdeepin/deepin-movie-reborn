// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stub/stub.h"
#include <gtest/gtest.h>
#include "cdlnasoappost.h"
/*******************************函数打桩************************************/

class UT_CDlnaSoapPost : public QObject, public ::testing::Test
{
public:
    UT_CDlnaSoapPost(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        m_tester = new CDlnaSoapPost();
    }

    virtual void TearDown()
    {
        delete m_tester;
    }

protected:
    CDlnaSoapPost *m_tester;
};

TEST_F(UT_CDlnaSoapPost, initTest)
{

}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_SetAVTransportURI)
{
    DlnaOper oper = DLNA_SetAVTransportURI;
    QString ControlURLPro = "";
    QString sHostUrl = "";
    QString sLocalUrl = "";
    m_tester->SoapOperPost(oper, ControlURLPro, sHostUrl, sLocalUrl);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Stop)
{
    DlnaOper oper = DLNA_Stop;
    QString ControlURLPro = "";
    QString sHostUrl = "";
    QString sLocalUrl = "";
    m_tester->SoapOperPost(oper, ControlURLPro, sHostUrl, sLocalUrl);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Pause)
{
    DlnaOper oper = DLNA_Pause;
    QString ControlURLPro = "";
    QString sHostUrl = "";
    QString sLocalUrl = "";
    m_tester->SoapOperPost(oper, ControlURLPro, sHostUrl, sLocalUrl);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Play)
{
    DlnaOper oper = DLNA_Play;
    QString ControlURLPro = "";
    QString sHostUrl = "";
    QString sLocalUrl = "";
    m_tester->SoapOperPost(oper, ControlURLPro, sHostUrl, sLocalUrl);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Seek)
{
    DlnaOper oper = DLNA_Seek;
    QString ControlURLPro = "";
    QString sHostUrl = "";
    QString sLocalUrl = "";
    int nSeek = 10;
    m_tester->SoapOperPost(oper, ControlURLPro, sHostUrl, sLocalUrl, nSeek);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_GetPositionInfo)
{
    DlnaOper oper = DLNA_GetPositionInfo;
    QString ControlURLPro = "";
    QString sHostUrl = "";
    QString sLocalUrl = "";
    m_tester->SoapOperPost(oper, ControlURLPro, sHostUrl, sLocalUrl);
}
