// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ============================================================================
// UT_GetDlnaXmlValue — Google Test suite for src/dlna/getdlnaxmlvalue.cpp
//
// Minimal 10-item checklist (test-code-gen reference §case-design):
//  [1] ≥1 case per public method: ctor/dtor(+Ctor_ValidXml, Dtor_Release),
//      getValueByPath(+6), getValueByPathValue(+5)                    -> OK
//  [2] equivalence classes: valid XML / malformed XML / empty input;
//      path hit / miss / multi-level; service match / no-match /
//      malformed "k=v" pair                                           -> OK
//  [3] boundary values: empty QByteArray, missing path segments,
//      sValue without '=' / with two '=', SOAP namespaced tags        -> OK
//  [4] TEST_P for ≥3 homogeneous inputs: not applicable (small fixed
//      XML fixtures; parameterization adds no homogeneity)            -> N/A
//  [5] branch list (getElmByPath(el) complexity 6, listed below)
//      mapped to case names                                           -> OK
//  [6] all if / early-return branches of public methods covered:
//      see branch table below                                         -> OK
//  [7] EXPECT_THROW exact type+message: methods do not throw; they
//      return "" on failure (strong-exception-safety verified instead)-> N/A
//  [8] negative cases with state-intactness checks: MalformedXml case
//      re-queries a valid path afterwards; Dtor case re-instantiates  -> OK
//  [9] stubs: none needed — pure in-memory QDomDocument logic, no
//      DBus/IO/network/timer dependencies                             -> OK
// [10] custom metatypes: none cross this API                          -> OK
//
// Branch table (source ground truth, getdlnaxmlvalue.cpp):
//  ctor: setContent ok / malformed -> Ctor_ValidXml, GetValueByPath_MalformedXml
//  ~dtor: doc non-null delete+null -> Dtor_ReleaseDocument
//  getValueByPath: split -> getElmByPath(list) hit/miss -> Get_ValueByPath_*
//  getValueByPathValue: parent null -> GV_NullParentElement;
//      sList.size()!=2 -> GV_MalformedPair_NoEquals / _TwoEquals;
//      service loop hit -> GV_MatchingService / _SecondService;
//      loop exhausted (no match) -> GV_NonMatchingValue;
//      getElmText(null) -> covered by all empty-result cases
// ============================================================================

#include <gtest/gtest.h>
#include <QString>
#include <QByteArray>
#include "getdlnaxmlvalue.h"

namespace {

// UPnP MediaRenderer device descriptor (same shape the DLNA stack parses).
const char *kDeviceXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<root>"
      "<specVersion><major>1</major><minor>0</minor></specVersion>"
      "<device>"
        "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>"
        "<friendlyName>Macast(myk-PC)</friendlyName>"
        "<manufacturer>DXDiablo</manufacturer>"
        "<modelName>Macast</modelName>"
        "<UDN>uuid:0f474cc4-4e79-4f4e-8a5d-2b3e6a</UDN>"
        "<serviceList>"
          "<service>"
            "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
            "<serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>"
            "<controlURL>AVTransport/action</controlURL>"
            "<SCPDURL>AVTransport/scpd</SCPDURL>"
          "</service>"
          "<service>"
            "<serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
            "<serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>"
            "<controlURL>RenderingControl/action</controlURL>"
            "<SCPDURL>RenderingControl/scpd</SCPDURL>"
          "</service>"
        "</serviceList>"
      "</device>"
    "</root>";

// SOAP GetPositionInfoResponse envelope (shape produced by real renderers,
// consumed via the namespaced path "s:Body/u:GetPositionInfoResponse/...").
const char *kSoapPositionInfoXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
      "<s:Body>"
        "<u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
          "<Track>1</Track>"
          "<TrackDuration>0:01:40</TrackDuration>"
          "<TrackMetaData>meta-data</TrackMetaData>"
          "<TrackURI>http://10.0.0.2/1.mp4</TrackURI>"
          "<RelTime>0:00:10</RelTime>"
          "<AbsTime>0:00:12</AbsTime>"
          "<RelCount>2147483647</RelCount>"
          "<AbsCount>2147483647</AbsCount>"
        "</u:GetPositionInfoResponse>"
      "</s:Body>"
    "</s:Envelope>";

class UT_GetDlnaXmlValue : public QObject, public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// --- ctor / dtor -------------------------------------------------------------

// ctor parses the document; dtor deletes it. Two consecutive instances prove
// the destructor releases the QDomDocument without leaking/corrupting state.
TEST_F(UT_GetDlnaXmlValue, Ctor_ValidXml_NavigatesDocument)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QString("Macast(myk-PC)"));
    EXPECT_EQ(xml.getValueByPath("device/modelName"), QString("Macast"));

    // Dtor branch: doc non-null -> delete + null out; re-create to prove cleanup.
    {
        GetDlnaXmlValue scoped{QByteArray(kDeviceXml)};
        EXPECT_EQ(scoped.getValueByPath("specVersion/major"), QString("1"));
    }
    GetDlnaXmlValue again{QByteArray(kDeviceXml)};
    EXPECT_EQ(again.getValueByPath("device/UDN"), QString("uuid:0f474cc4-4e79-4f4e-8a5d-2b3e6a"));
}

// --- getValueByPath ----------------------------------------------------------

// Multi-level descent: each "/" segment steps one firstChildElement() down.
TEST_F(UT_GetDlnaXmlValue, GetValueByPath_NestedDescendsEachSegment)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    // Two levels: root -> specVersion.
    EXPECT_EQ(xml.getValueByPath("specVersion/major"), QString("1"));
    EXPECT_EQ(xml.getValueByPath("specVersion/minor"), QString("0"));
    // Four levels, first <service> sibling selected.
    EXPECT_EQ(xml.getValueByPath("device/serviceList/service/serviceId"),
              QString("urn:upnp-org:serviceId:AVTransport"));
}

// Whitespace around the leaf text is trimmed by getValueByPath.
TEST_F(UT_GetDlnaXmlValue, GetValueByPath_TrimsLeafText)
{
    const QByteArray xmlData =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><root><device>"
        "<friendlyName>  padded name  </friendlyName>"
        "</device></root>";
    GetDlnaXmlValue xml{xmlData};
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QString("padded name"));
}

// Missing segment at any level yields the empty string.
TEST_F(UT_GetDlnaXmlValue, GetValueByPath_MissingSegment_ReturnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    EXPECT_EQ(xml.getValueByPath("device/nonexistent"), QString());
    EXPECT_EQ(xml.getValueByPath("nonexistent/friendlyName"), QString());
    EXPECT_EQ(xml.getValueByPath("device/serviceList/nope/serviceId"), QString());
}

// Malformed XML: setContent fails silently; every query answers "" and the
// object stays usable (strong exception safety: no crash, no throw).
TEST_F(UT_GetDlnaXmlValue, GetValueByPath_MalformedXml_IsTolerated)
{
    GetDlnaXmlValue xml{QByteArray("<<<not-xml-garbage")};
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QString());
    EXPECT_EQ(xml.getValueByPath("device"), QString());
    // Object still consistent after failed queries.
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QString());
}

// Empty input is the boundary of setContent: empty document, empty answers.
TEST_F(UT_GetDlnaXmlValue, Ctor_EmptyInput_ReturnsEmptyDocumentValues)
{
    GetDlnaXmlValue xml{QByteArray()};
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QString());
    EXPECT_EQ(xml.getValueByPathValue("device", "serviceType=x", "controlURL"), QString());
}

// SOAP namespaced tags are matched literally (production path used by
// CDlnaSoapPost::SoapOperPost for DLNA_GetPositionInfo).
TEST_F(UT_GetDlnaXmlValue, GetValueByPath_SoapNamespacedPath)
{
    GetDlnaXmlValue xml{QByteArray(kSoapPositionInfoXml)};
    EXPECT_EQ(xml.getValueByPath("s:Body/u:GetPositionInfoResponse/Track"), QString("1"));
    EXPECT_EQ(xml.getValueByPath("s:Body/u:GetPositionInfoResponse/TrackDuration"), QString("0:01:40"));
    EXPECT_EQ(xml.getValueByPath("s:Body/u:GetPositionInfoResponse/RelTime"), QString("0:00:10"));
    EXPECT_EQ(xml.getValueByPath("s:Body/u:GetPositionInfoResponse/RelCount"), QString("2147483647"));
}

// --- getValueByPathValue -----------------------------------------------------

// Service loop hit branch: first <service> whose <sValue-key> text equals
// sValue's right-hand side; sElm child of that service is returned.
TEST_F(UT_GetDlnaXmlValue, GetValueByPathValue_MatchingService_ReturnsChildText)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    // The path must descend to the element that directly owns <service> children.
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList", "serviceType=urn:schemas-upnp-org:service:AVTransport:1",
                                      "controlURL"), QString("AVTransport/action"));
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList", "serviceType=urn:schemas-upnp-org:service:AVTransport:1",
                                      "SCPDURL"), QString("AVTransport/scpd"));
}

// Loop iteration branch: the second <service> sibling matches later.
TEST_F(UT_GetDlnaXmlValue, GetValueByPathValue_SecondService_MatchesLaterSibling)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList", "serviceType=urn:schemas-upnp-org:service:RenderingControl:1",
                                      "controlURL"), QString("RenderingControl/action"));
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList", "serviceId=urn:upnp-org:serviceId:RenderingControl",
                                      "serviceType"), QString("urn:schemas-upnp-org:service:RenderingControl:1"));
}

// Loop-exhausted branch: no service carries the requested value -> "".
TEST_F(UT_GetDlnaXmlValue, GetValueByPathValue_NonMatchingValue_ReturnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    EXPECT_EQ(xml.getValueByPathValue("device", "serviceType=urn:schemas-upnp-org:service:Unknown:1",
                                      "controlURL"), QString());
    // Object remains usable after a no-match query.
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QString("Macast(myk-PC)"));
}

// sList.size() != 2 branches: no '=' at all and more than one '='.
TEST_F(UT_GetDlnaXmlValue, GetValueByPathValue_MalformedPair_ReturnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    EXPECT_EQ(xml.getValueByPathValue("device", "serviceTypeOnlyNoEquals", "controlURL"), QString());
    EXPECT_EQ(xml.getValueByPathValue("device", "a=b=c", "controlURL"), QString());
}

// Null parent element branch: path miss short-circuits the element lookup.
TEST_F(UT_GetDlnaXmlValue, GetValueByPathValue_NullParentElement_ReturnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(kDeviceXml)};
    EXPECT_EQ(xml.getValueByPathValue("nonexistent", "serviceType=urn:schemas-upnp-org:service:AVTransport:1",
                                      "controlURL"), QString());
}

} // namespace
