// Copyright (C) 2016 ~ 2018 Wuhan Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VideoBOXBUTTON_H
#define VideoBOXBUTTON_H
#pragma once

#include <QScopedPointer>
    #include <QMap>
#include <QVariant>

#include <DPushButton>
#include <DButtonBox>

DWIDGET_USE_NAMESPACE

class VideoBoxButton : public DButtonBoxButton
{
    struct VideoPicPathInfo {
        QString normalPicPath;
        QString hoverPicPath;
        QString pressPicPath;
        QString checkedPicPath;
    };

    struct QVariantCompare {
        bool operator()(const QVariant& v1, const QVariant& v2) const {
            return v1.toString() < v2.toString();
        }
    };

    struct PropertyPicPaths {
        QString first;
        std::map<QVariant, VideoPicPathInfo, QVariantCompare> second;  // 使用自定义比较器
    };

    Q_OBJECT
public:
    explicit VideoBoxButton(const QString &text, QWidget *parent = Q_NULLPTR);

    VideoBoxButton(const QString &text, const QString &normalPic, const QString &hoverPic,
                   const QString &pressPic, const QString &checkedPic = QString(), QWidget *parent = nullptr);

//    cppcheck修改
//    void setPropertyPic(QString propertyName, const QVariant &value, const QString &normalPic, const QString &hoverPic,
//                        const QString &pressPic, const QString &checkedPic = QString());
//    void setPropertyPic(const QString &normalPic, const QString &hoverPic,
//                        const QString &pressPic, const QString &checkedPic = QString());

    void setTransparent(bool flag);
    void setAutoChecked(bool flag);

protected:
    //void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEvent *event) override;
#else
    void enterEvent(QEnterEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    char                                               status                  = 0;
    bool                                               autoChecked             = false;
    VideoPicPathInfo                                   defaultPicPath;
    bool                                               transparent             = true;
    PropertyPicPaths                                   propertyPicPaths;
};


#endif // VideoBOXBUTTON_H








