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
#include "getdlnaxmlvalue.h"
/*******************************函数打桩************************************/

class UT_GetDlnaXmlValue : public QObject, public ::testing::Test
{
public:
    UT_GetDlnaXmlValue(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        QByteArray data = " \
            <root xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\" xmlns=\"urn:schemas-upnp-org:device-1-0\"> \
            <specVersion> \
            <major>1</major> \
            <minor>0</minor> \
            </specVersion> \
            <device> \
            <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType> \
            <UDN>uuid:3391800f-5753-4b50-b6a2-633a711bd2bf</UDN> \
            <friendlyName>Macast(myk-PC)</friendlyName> \
            <manufacturer>xfangfang</manufacturer> \
            <manufacturerURL>https://github.com/xfangfang</manufacturerURL> \
            <modelDescription>AVTransport Media Renderer</modelDescription> \
            <modelName>Macast</modelName> \
            <modelNumber>0.7</modelNumber> \
            <modelURL>https://xfangfang.github.io/Macast</modelURL> \
            <serialNumber>1024</serialNumber> \
            <dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMR-1.50</dlna:X_DLNADOC> \
            <serviceList> \
            <service> \
            <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType> \
            <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId> \
            <controlURL>AVTransport/action</controlURL> \
            <eventSubURL>AVTransport/event</eventSubURL> \
            <SCPDURL>dlna/AVTransport.xml</SCPDURL> \
            </service> \
            <service> \
            <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType> \
            <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId> \
            <controlURL>RenderingControl/action</controlURL> \
            <eventSubURL>RenderingControl/event</eventSubURL> \
            <SCPDURL>dlna/RenderingControl.xml</SCPDURL> \
            </service> \
            <service> \
            <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType> \
            <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId> \
            <controlURL>ConnectionManager/action</controlURL> \
            <eventSubURL>ConnectionManager/event</eventSubURL> \
            <SCPDURL>dlna/ConnectionManager.xml</SCPDURL> \
            </service> \
            </serviceList> \
            </device> \
            </root>";
        m_tester = new GetDlnaXmlValue(data);
    }

    virtual void TearDown()
    {
        delete m_tester;
    }

protected:
    GetDlnaXmlValue *m_tester;
};

TEST_F(UT_GetDlnaXmlValue, initTest)
{

}

TEST_F(UT_GetDlnaXmlValue, getValueByPath)
{
    EXPECT_EQ(m_tester->getValueByPath("device/friendlyName"), "Macast(myk-PC)");
}

TEST_F(UT_GetDlnaXmlValue, getValueByPath_1)
{
    EXPECT_EQ(m_tester->getValueByPathValue(
                  "device/serviceList",
                  "serviceType=urn:schemas-upnp-org:service:AVTransport:1",
                  "controlURL"),
                  "AVTransport/action");
}
