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
#include "movieinfo_dialog.h"
#include "mpv_proxy.h"
#include "playlist_model.h"
#include "utils.h"
#include <QDebug>

DWIDGET_USE_NAMESPACE

namespace dmr {
MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo& pif)
    :DAbstractDialog(nullptr)
{
    setFixedSize(300, 441);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    auto layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 10);
    setLayout(layout);

    DImageButton* closeBt = new DImageButton(this);
    closeBt->setFixedSize(50, 50);
    connect(closeBt, &DImageButton::clicked, this, &MovieInfoDialog::close);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);

    const auto& mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(10, 0, 10, 0);
    ml->setSpacing(0);
    layout->addLayout(ml);

    auto *pm = new PosterFrame(this);
    pm->setFixedSize(220, 128);

    auto dpr = qApp->devicePixelRatio();
    QPixmap cover;
    if (pif.thumbnail.isNull()) {
        cover = (utils::LoadHiDPIPixmap(LOGO_BIG));
    } else {
        QSize sz(220, 128);
        sz *= dpr;
        auto img = pif.thumbnail.scaledToWidth(sz.width(), Qt::SmoothTransformation);
        cover = img.copy(0, (img.height()-sz.height())/2, sz.width(), sz.height());
        cover.setDevicePixelRatio(dpr);
    }
    cover = utils::MakeRoundedPixmap(cover, 4, 4);
    pm->setPixmap(cover);
    pm->ensurePolished();
    ml->addWidget(pm);
    ml->setAlignment(pm, Qt::AlignHCenter);
    ml->addSpacing(10);

    auto *nm = new DLabel(this);
    nm->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T8));
    DPalette pal_nm = DApplicationHelper::instance()->palette(nm);
    pal_nm.setBrush(DPalette::WindowText, pal_nm.color(DPalette::TextLively));
    nm->setPalette(pal_nm);
    nm->setText(nm->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 260));
    ml->addWidget(nm);
    ml->setAlignment(nm, Qt::AlignHCenter);
    ml->addSpacing(44);

    InfoBottom *infoRect = new InfoBottom;
    infoRect->setFixedSize(280, 181);
    ml->addWidget(infoRect);
    ml->setAlignment(infoRect, Qt::AlignHCenter);
    ml->addSpacing(10);

    auto *infolyt = new QHBoxLayout(infoRect);
    infolyt->setContentsMargins(10, 0, 0, 30);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 5, 0, 0);
    infolyt->addLayout(form);
    infolyt->setAlignment(infolyt, Qt::AlignHCenter);
    
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignCenter);
//    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

#define ADD_ROW(title, field)  do { \
    QFont font(DFontSizeManager::instance()->get(DFontSizeManager::T8)); \
    auto f = new DLabel(field, this); \
    f->setFont(font); \
    DPalette pal_f = DApplicationHelper::instance()->palette(f); \
    pal_f.setBrush(DPalette::WindowText, pal_f.color(DPalette::TextLively)); \
    f->setPalette(pal_f); \
    auto t = new DLabel(title, this); \
    t->setAlignment(Qt::AlignLeft); \
    t->setWordWrap(true); \
    t->setFont(font); \
    form->addRow(t, f); \
} while (0)

    auto title = new DLabel(MV_BASE_INFO, this);
    title->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    DPalette pal_title = DApplicationHelper::instance()->palette(title);
    pal_title.setBrush(DPalette::WindowText, pal_title.color(DPalette::TextLively));
    title->setPalette(pal_title);
    form->addRow(title);

    ADD_ROW(MV_RESOLUTION, mi.resolution);
    ADD_ROW(MV_FILE_TYPE, mi.fileType);
    ADD_ROW(MV_FILE_SIZE, mi.sizeStr());
    ADD_ROW(MV_DURATION, mi.durationStr());

    auto fm = nm->fontMetrics();
    auto fp = utils::ElideText(mi.filePath, {200, 40}, QTextOption::WordWrap,
            nm->font(), Qt::ElideNone, fm.height(), 150);
    ADD_ROW(MV_FILE_PATH, fp);

#undef ADD_ROW

//    QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged,this,
//                         [=] () {
        DPalette pal = DApplicationHelper::instance()->palette(this);
        if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            pal.setBrush(DPalette::Window, pal.color(DPalette::ItemBackground));

            closeBt->setNormalPic(INFO_CLOSE_LIGHT);
            infoRect->setInfoBgTheme(lightTheme);

            qDebug() << ".............111111";
        }
        else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            pal.setBrush(DPalette::Window, pal.color(DPalette::ItemBackground));

            closeBt->setNormalPic(INFO_CLOSE_DARK);
            infoRect->setInfoBgTheme(darkTheme);

            qDebug() << ".............222222";
        }
        else {
            pal.setBrush(DPalette::Window, pal.color(DPalette::ItemBackground));

            closeBt->setNormalPic(INFO_CLOSE_DARK);
            infoRect->setInfoBgTheme(darkTheme);

            qDebug() << ".............333333";
        }
        this->setPalette(pal);
//    });

#if DTK_VERSION > DTK_VERSION_CHECK(2, 0, 6, 0)
    DThemeManager::instance()->setTheme(this, "light");
    DThemeManager::instance()->setTheme(closeBt, "light");
#else
//    DThemeManager::instance()->registerWidget(this);
//    closeBt->setStyleSheet(DThemeManager::instance()->getQssForWidget("DWindowCloseButton", "light"));
#endif
}

InfoBottom::InfoBottom()
{
}

void InfoBottom::setInfoBgTheme(ThemeTYpe themeType)
{
    m_themeType = themeType;
}

void InfoBottom::paintEvent(QPaintEvent *ev)
{
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing);

    if (lightTheme == m_themeType) {
        pt.setPen(QColor(0, 0, 0, 5));
        pt.setBrush(QBrush(QColor(249, 249, 249, 160)));
    }
    else if (darkTheme == m_themeType) {
        pt.setPen(QColor(0, 0, 0, 5));
        pt.setBrush(QBrush(QColor(249, 249, 249, 160)));
    }
    else {
        pt.setPen(QColor(0, 0, 0, 5));
        pt.setBrush(QBrush(QColor(249, 249, 249, 160)));
    }

    QRect rect = this->rect();
    rect.setWidth(rect.width() - 1);
    rect.setHeight(rect.height() - 1);

    QPainterPath painterPath;
    painterPath.addRoundedRect(rect, 10, 10);
    pt.drawPath(painterPath);

}

}
