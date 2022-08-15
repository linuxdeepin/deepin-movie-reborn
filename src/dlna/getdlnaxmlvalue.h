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
#ifndef GETDLNAXMLVALUE_H
#define GETDLNAXMLVALUE_H

#include <QObject>
#include <QDomDocument>
#include <QDomElement>
#include <QStringList>

class GetDlnaXmlValue : public QObject
{
    Q_OBJECT
public:
    GetDlnaXmlValue(QByteArray data = "");
    ~GetDlnaXmlValue();
    QString getValueByPath(QString sPath);
    QString getValueByPathValue(QString sPath, QString sValue, QString sElm);

private:
    QDomDocument *doc;
private:
    QDomElement getElmByPath(QStringList sList);
    //查找elm 下 属性值为sValue的下的sElm元素的值
    //"./device/serviceList/service/"
    //"[serviceType='{0}']/controlURL"
    QDomElement getElmByPath(QDomElement elm, QString sValue, QString sElm);
    QString getElmText(QDomElement elm);
};

#endif // GETDLNAXMLVALUE_H
