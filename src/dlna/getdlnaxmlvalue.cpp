/*
 * Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "getdlnaxmlvalue.h"



GetDlnaXmlValue::GetDlnaXmlValue(QByteArray data)
{
    doc = new QDomDocument;
    doc->setContent(data);
}

GetDlnaXmlValue::~GetDlnaXmlValue()
{
    if(doc) {
       delete doc;
        doc = NULL;
    }
}

QString GetDlnaXmlValue::getValueByPath(QString sPath)
{
    QStringList sList = sPath.split("/");
    QDomElement elm = getElmByPath(sList);
    return getElmText(elm).trimmed();
}

QString GetDlnaXmlValue::getValueByPathValue(QString sPath, QString sValue, QString sElm)
{
    QStringList sList = sPath.split("/");
    QDomElement elm = getElmByPath(sList);
    QDomElement childElm = getElmByPath(elm, sValue, sElm);
    return getElmText(childElm).trimmed();
}

QDomElement GetDlnaXmlValue::getElmByPath(QStringList sList)
{
    QDomNode node = doc->firstChild();
    QDomElement elm = node.toElement();
    if(elm.isNull())
    {
        node = node.nextSibling();
        elm = node.toElement();
        if(elm.isNull())
            return QDomElement();
    }
    foreach(QString tagName, sList) {
        elm = elm.firstChildElement(tagName);
        if(elm.isNull())
        {
            return QDomElement();
        }
    }
    return elm;
}

QDomElement GetDlnaXmlValue::getElmByPath(QDomElement childElm, QString sValue, QString sElmText)
{
    if(childElm.isNull()) return QDomElement();
    QStringList sList = sValue.split("=");
    if(sList.size() != 2) return QDomElement();
    QDomElement elm = childElm.firstChildElement("service");;
    while(!elm.isNull()) {
        QDomElement matchElm = elm.firstChildElement(sList[0]);
        QString sText = getElmText(matchElm);
        if(sText == sList[1]) {
            return elm.firstChildElement(sElmText);
        }
        elm = elm.nextSibling().toElement();
    }
    return QDomElement();
}

QString GetDlnaXmlValue::getElmText(QDomElement elm)
{
    if(!elm.isNull()) {
        return elm.text();
    }
    return "";
}

