/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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
#ifndef _DMR_MOVIE_INFO_DIALOG_H
#define _DMR_MOVIE_INFO_DIALOG_H

#include <QtWidgets>
#include <QGuiApplication>
#include <DDialog>
#include <DImageButton>
#include <DPushButton>
#include <DLabel>
#include <DFontSizeManager>
#include <DThemeManager>
#include <DApplicationHelper>
#include <DGuiApplicationHelper>
#include <DApplication>
#include <DArrowLineDrawer>

#define LINE_MAX_WIDTH 200
#define LINE_HEIGHT 30
const QString LOGO_BIG = ":/resources/icons/logo-big.svg";
const QString INFO_CLOSE_LIGHT = ":/resources/icons/light/info_close_light.svg";
const QString INFO_CLOSE_DARK = ":/resources/icons/dark/info_close_dark.svg";

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

namespace dmr {
struct PlayItemInfo;
class PosterFrame: public QLabel
{
    Q_OBJECT
public:
    PosterFrame(QWidget *parent) : QLabel(parent)
    {
        auto e = new QGraphicsDropShadowEffect(this);
        e->setColor(QColor(0, 0, 0, 76));
        e->setOffset(0, 3);
        e->setBlurRadius(6);
        setGraphicsEffect(e);
    }
};

class InfoBottom: public DFrame
{
    Q_OBJECT
public:
    InfoBottom() {}

protected:
    virtual void paintEvent(QPaintEvent *ev)
    {
        QPainter pt(this);
        pt.setRenderHint(QPainter::Antialiasing);

        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            pt.setPen(Qt::NoPen);
            pt.setBrush(QBrush(QColor(255, 255, 255, 0)));
        } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            pt.setPen(Qt::NoPen);
            pt.setBrush(QBrush(QColor(45, 45, 45, 0)));
        }

        QRect rect = this->rect();
        rect.setWidth(rect.width() - 1);
        rect.setHeight(rect.height() - 1);

        QPainterPath painterPath;
        painterPath.addRoundedRect(rect, 10, 10);
        pt.drawPath(painterPath);
    }
};

class ArrowLine: public DArrowLineDrawer
{
    Q_OBJECT
public:
    ArrowLine() {}

protected:
    virtual void paintEvent(QPaintEvent *ev)
    {
        QPainter pt(this);
        pt.setRenderHint(QPainter::Antialiasing);

        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            pt.setPen(QColor(0, 0, 0, 0.05 * 255));
            pt.setBrush(QBrush(QColor(255, 255, 255, 255 * 0.7)));
        } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            pt.setPen(QColor(255, 255, 255, 20));
            pt.setBrush(QBrush(QColor(45, 45, 45, 250 * 0.7)));
        }

        QRect rect = this->rect();
        rect.setWidth(rect.width() - 1);
        rect.setHeight(rect.height() - 1);

        QPainterPath painterPath;
        painterPath.addRoundedRect(rect, 10, 10);
        pt.drawPath(painterPath);
    }
};

class CloseButton : public DPushButton
{
public:
    CloseButton(QWidget *parent) {}
protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPainter painter(this);
        QRect rect = this->rect();
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            painter.drawPixmap(rect, QPixmap(INFO_CLOSE_LIGHT));
        } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            painter.drawPixmap(rect, QPixmap(INFO_CLOSE_DARK));
        } else {
            painter.drawPixmap(rect, QPixmap(INFO_CLOSE_LIGHT));
        }
    }
};

class MovieInfoDialog: public DAbstractDialog
{
    Q_OBJECT
public:
    MovieInfoDialog(const struct PlayItemInfo &,QWidget *);

protected:
    void paintEvent(QPaintEvent *ev);

private slots:
    void OnFontChanged(const QFont &font);
    void changedHeight(const int);

private:
    QList<DDrawer *> addExpandWidget(const QStringList &titleList);
    void initExpand(QVBoxLayout *layout, DDrawer *expand);
    void addRow(QString, QString, QFormLayout *, QList<DLabel *> &);

private:
    DLabel *m_fileNameLbl {nullptr};
    DLabel *m_filePathLbl {nullptr};
    QString m_strFilePath {QString()};
    QList<DLabel *> m_titleList;
    QList<DDrawer *> m_expandGroup;
    QScrollArea *m_scrollArea;
    int lastHeight {-1};
};


}

#endif /* ifndef _DMR_MOVIE_INFO_DIALOG_H */
