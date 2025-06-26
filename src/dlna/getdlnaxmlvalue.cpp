// Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "getdlnaxmlvalue.h"
#include <QDebug>



GetDlnaXmlValue::GetDlnaXmlValue(QByteArray data)
{
    qDebug() << "Entering GetDlnaXmlValue constructor. Data size:" << data.size() << "bytes.";
    doc = new QDomDocument;
    doc->setContent(data);

    qDebug() << "Exiting GetDlnaXmlValue constructor.";
}

GetDlnaXmlValue::~GetDlnaXmlValue()
{
    qDebug() << "Entering GetDlnaXmlValue destructor.";
    if(doc) {
       delete doc;
        doc = NULL;
        qDebug() << "QDomDocument deleted and set to NULL.";
    }
    qDebug() << "Exiting GetDlnaXmlValue destructor.";
}

QString GetDlnaXmlValue::getValueByPath(QString sPath)
{
    qDebug() << "Entering getValueByPath function. Path:" << sPath;
    QStringList sList = sPath.split("/");
    QDomElement elm = getElmByPath(sList);
    return getElmText(elm).trimmed();
}
/**
 * @brief getValueByPathValue 根据xml节点的值查找当前节点下的指定节点的值
 * @param sPath 查找xml节点路径
 * @param sValue 查找xml节点值
 * @param sElm 函数需要返回节点的值需要
 */
QString GetDlnaXmlValue::getValueByPathValue(QString sPath, QString sValue, QString sElm)
{
    qDebug() << "Entering getValueByPathValue function. Path:" << sPath << "Value:" << sValue << "Elm:" << sElm;
    QStringList sList = sPath.split("/");
    QDomElement elm = getElmByPath(sList);
    QDomElement childElm = getElmByPath(elm, sValue, sElm);
    return getElmText(childElm).trimmed();
}
/**
 * @brief getElmByPath 获取路径的节点
 * @param sList 查找xml节点路径
 */
QDomElement GetDlnaXmlValue::getElmByPath(QStringList sList)
{
    qDebug() << "Entering getElmByPath function. Path:" << sList.join("/");
    QDomNode node = doc->firstChild();
    QDomElement elm = node.toElement();
    if(elm.isNull())
    {
        node = node.nextSibling();
        elm = node.toElement();
        if(elm.isNull())
        {
            qDebug() << "elm is null";
            return QDomElement();
        }
    }
    foreach(QString tagName, sList) {
        elm = elm.firstChildElement(tagName);
        if(elm.isNull())
        {
            qDebug() << "elm is null";
            return QDomElement();
        }
    }
    qDebug() << "Exiting getElmByPath function. Found element:" << elm.tagName();
    return elm;
}
/**
 * @brief getElmByPath 获取路径的节点
 * @param elm 查找xml节点路径
 * @param sValue 查找xml节点路径
 * @param sElm 查找xml节点路径
 */
QDomElement GetDlnaXmlValue::getElmByPath(QDomElement childElm, QString sValue, QString sElmText)
{
    qDebug() << "Entering getElmByPath function. Path:" << sValue << sElmText;
    if(childElm.isNull()) {
        qDebug() << "childElm is null";
        return QDomElement();
    }
    QStringList sList = sValue.split("=");
    if(sList.size() != 2) {
        qDebug() << "sList.size() != 2";
        return QDomElement();
    }
    QDomElement elm = childElm.firstChildElement("service");;
    while(!elm.isNull()) {
        QDomElement matchElm = elm.firstChildElement(sList[0]);
        QString sText = getElmText(matchElm);
        if(sText == sList[1]) {
            qDebug() << "Exiting getElmByPath function. Found matching element:" << elm.tagName();
            return elm.firstChildElement(sElmText);
        }
        elm = elm.nextSibling().toElement();
    }
    qDebug() << "Exiting getElmByPath function. No matching element found.";
    return QDomElement();
}
/**
 * @brief getElmText 获取节点的值
 * @param elm 查找xml节点路径
 */
QString GetDlnaXmlValue::getElmText(QDomElement elm)
{
    qDebug() << "Entering getElmText function. Element tag name:" << elm.tagName();
    if(!elm.isNull()) {
        qDebug() << "Element text:" << elm.text();
        return elm.text();
    }
    qDebug() << "Exiting getElmText function. Element is NULL.";
    return "";
}

