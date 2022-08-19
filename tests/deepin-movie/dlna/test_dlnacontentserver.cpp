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
#include "dlnacontentserver.h"

void qThread_start_stub()
{
    return;
}

void stub_QThread_start(Stub &stub)
{
    stub.set(ADDR(QThread, start), qThread_start_stub);
}
/*******************************函数打桩************************************/

class UT_DlnaContentServer : public QObject, public ::testing::Test
{
public:
    UT_DlnaContentServer(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        Stub stub;
        stub_QThread_start(stub);
        m_tester = new DlnaContentServer();
    }

    virtual void TearDown()
    {
        delete m_tester;
    }

protected:
    DlnaContentServer *m_tester;
};


class UT_DlnaContentServerRange : public QObject, public ::testing::Test
{
public:
    UT_DlnaContentServerRange(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        m_tester = new DlnaContentServer::Range();
    }

    virtual void TearDown()
    {
        delete m_tester;
    }
protected:
    DlnaContentServer::Range *m_tester;
};

TEST_F(UT_DlnaContentServerRange, initTest)
{
}

TEST_F(UT_DlnaContentServerRange, runTest)
{
    DlnaContentServer::Range range1 = {-1, -1, 0}, range2 = {-1, -1, 0};
    if(range1 == range2) {
        range1.full();
        range1.rangeLength();
        m_tester->fromRange("bytes=10-");
    }
}

TEST_F(UT_DlnaContentServerRange, runTest_1)
{
    m_tester->fromRange("bytes=10-100", 91);
}


TEST_F(UT_DlnaContentServer, initTest)
{
}

TEST_F(UT_DlnaContentServer, initializeHttpServer)
{
    EXPECT_EQ(m_tester->initializeHttpServer(9090),true);
}

TEST_F(UT_DlnaContentServer, streamFile)
{
    const QString path = "";
    const QString mime;
    m_tester->streamFile(path, mime, nullptr, nullptr);
}

TEST_F(UT_DlnaContentServer, streamFileRange)
{
    const QString path = "";
    auto file = std::make_shared<QFile>(path);
    m_tester->streamFileRange(file, nullptr, nullptr);
}

TEST_F(UT_DlnaContentServer, streamFileNoRange)
{
    const QString path = "";
    auto file = std::make_shared<QFile>(path);
    m_tester->streamFileNoRange(file, nullptr, nullptr);
}

TEST_F(UT_DlnaContentServer, seqWriteData)
{
    const QString path = "";
    auto file = std::make_shared<QFile>(path);
    QHttpResponse *resp = nullptr;
    m_tester->seqWriteData(file, 0, resp);
}

TEST_F(UT_DlnaContentServer, dlnaContentFeaturesHeader)
{
    QString mine = "video/x-msvideo";
    m_tester->dlnaContentFeaturesHeader(mine);
}

TEST_F(UT_DlnaContentServer, dlnaContentFeaturesHeader_1)
{
    QString mine = "video/x-msvideo";
    m_tester->dlnaContentFeaturesHeader(mine, false, false);
}


TEST_F(UT_DlnaContentServer, dlnaContentFeaturesHeader_empty)
{
    QString mine = "xxxx";
    m_tester->dlnaContentFeaturesHeader(mine);
}

TEST_F(UT_DlnaContentServer, dlnaContentFeaturesHeader_empty_1)
{
    QString mine = "xxxx";
    m_tester->dlnaContentFeaturesHeader(mine, false, false);
}

TEST_F(UT_DlnaContentServer, dlnaOrgPnFlags)
{
    QString mine = "video/x-msvideo";
    m_tester->dlnaOrgPnFlags(mine);
}

TEST_F(UT_DlnaContentServer, dlnaOrgPnFlags_aac)
{
    QString mine = "audio/aac";
    m_tester->dlnaOrgPnFlags(mine);
}

TEST_F(UT_DlnaContentServer, dlnaOrgPnFlags_mpeg)
{
    QString mine = "audio/mpeg";
    m_tester->dlnaOrgPnFlags(mine);
}

TEST_F(UT_DlnaContentServer, dlnaOrgPnFlags_wav)
{
    QString mine = "audio/vnd.wav";
    m_tester->dlnaOrgPnFlags(mine);
}

TEST_F(UT_DlnaContentServer, dlnaOrgPnFlags_L16)
{
    QString mine = "audio/L16";
    m_tester->dlnaOrgPnFlags(mine);
}

TEST_F(UT_DlnaContentServer, dlnaOrgPnFlags_matroska)
{
    QString mine = "video/x-matroska";
    m_tester->dlnaOrgPnFlags(mine);
}

TEST_F(UT_DlnaContentServer, dlnaOrgFlagsForFile)
{
    m_tester->dlnaOrgFlagsForFile();
}

TEST_F(UT_DlnaContentServer, dlnaOrgFlagsForStreaming)
{
    m_tester->dlnaOrgFlagsForStreaming();
}

TEST_F(UT_DlnaContentServer, setBaseUrl)
{
    m_tester->setBaseUrl("");
}

TEST_F(UT_DlnaContentServer, setDlnaFileName)
{
    m_tester->setDlnaFileName("");
}

TEST_F(UT_DlnaContentServer, getBaseUrl)
{
     EXPECT_EQ(m_tester->getBaseUrl(), "");
}

TEST_F(UT_DlnaContentServer, getIsStartHttpServer)
{
    m_tester->getIsStartHttpServer();
}

TEST_F(UT_DlnaContentServer, sendEmptyResponse)
{
    m_tester->sendEmptyResponse(nullptr, 0);
}
