/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     muyuankai <muyuankai@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
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
