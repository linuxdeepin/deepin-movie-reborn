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
    pal_nm.setBrush(DPalette::Text, pal_nm.color(DPalette::TextTitle));
    nm->setPalette(pal_nm);
    nm->setText(nm->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 260));
    ml->addWidget(nm);
    ml->setAlignment(nm, Qt::AlignHCenter);
    ml->addSpacing(44);

    InfoBottom *infoRect = new InfoBottom;
//    DWidget *infoRect = new DWidget;
//    DPalette pal_infoRect = DApplicationHelper::instance()->palette(infoRect);
//    pal_infoRect.setBrush(DPalette::Background, pal_infoRect.color(DPalette::ItemBackground));
//    infoRect->setPalette(pal_infoRect);
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

//        f->setFixedHeight(40);
#define ADD_ROW(title, field)  do { \
    QFont font(DFontSizeManager::instance()->get(DFontSizeManager::T8)); \
    auto f = new DLabel(title, this); \
    f->setFixedSize(55, 45); \
    f->setAlignment(Qt::AlignLeft | Qt::AlignTop); \
    f->setFont(font); \
    DPalette pal_f = DApplicationHelper::instance()->palette(f); \
    pal_f.setBrush(DPalette::Text, pal_f.color(DPalette::TextTitle)); \
    f->setPalette(pal_f); \
    auto t = new DLabel(field, this); \
    t->setFixedSize(200, 45); \
    t->setAlignment(Qt::AlignLeft | Qt::AlignTop); \
    t->setWordWrap(true); \
    t->setFont(font); \
    DPalette pal_t = DApplicationHelper::instance()->palette(t); \
    pal_t.setBrush(DPalette::Text, pal_t.color(DPalette::TextTitle)); \
    t->setPalette(pal_t); \
    form->addRow(f, t); \
} while (0)

    auto title = new DLabel(MV_BASE_INFO, this);
    title->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    DPalette pal_title = DApplicationHelper::instance()->palette(title);
    pal_title.setBrush(DPalette::Text, pal_title.color(DPalette::TextTitle));
    title->setPalette(pal_title);
    form->addRow(title);

    ADD_ROW(MV_RESOLUTION, mi.resolution);
    ADD_ROW(MV_FILE_TYPE, mi.fileType);
    ADD_ROW(MV_FILE_SIZE, mi.sizeStr());
    ADD_ROW(MV_DURATION, mi.durationStr());

    auto fm = nm->fontMetrics();
    auto fp = utils::ElideText(mi.filePath, {220, 40}, QTextOption::WordWrap,
            nm->font(), Qt::ElideNone, fm.height(), 150);
    ADD_ROW(MV_FILE_PATH, mi.filePath);

#undef ADD_ROW

    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [=]{
        DPalette pal_nm = DApplicationHelper::instance()->palette(nm);
        pal_nm.setBrush(DPalette::Text, pal_nm.color(DPalette::TextTitle));
        nm->setPalette(pal_nm);

        DPalette pal_title = DApplicationHelper::instance()->palette(title);
        pal_title.setBrush(DPalette::Text, pal_title.color(DPalette::TextTitle));
        title->setPalette(pal_title);
    });

    if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
        infoRect->setInfoBgTheme(lightTheme);
    }
    else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        closeBt->setNormalPic(INFO_CLOSE_DARK);
        infoRect->setInfoBgTheme(darkTheme);
    }
    else {
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
        infoRect->setInfoBgTheme(lightTheme);
    }

//#if DTK_VERSION > DTK_VERSION_CHECK(2, 0, 6, 0)
//    DThemeManager::instance()->setTheme(this, "light");
//    DThemeManager::instance()->setTheme(closeBt, "light");
//#else
////    DThemeManager::instance()->registerWidget(this);
////    closeBt->setStyleSheet(DThemeManager::instance()->getQssForWidget("DWindowCloseButton", "light"));
//#endif
}

InfoBottom::InfoBottom()
{
}

void InfoBottom::setInfoBgTheme(ThemeTYpe themeType)
{
    m_themeType = themeType;
    update();
}

void InfoBottom::paintEvent(QPaintEvent *ev)
{
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing);

    if (lightTheme == m_themeType) {
        pt.setPen(QColor(0, 0, 0, 20));
        pt.setBrush(QBrush(QColor(250, 250, 250, 180)));
    }
    else if (darkTheme == m_themeType) {
        pt.setPen(QColor(255, 255, 255, 20));
        pt.setBrush(QBrush(QColor(80, 80, 80, 60)));
    }

    QRect rect = this->rect();
    rect.setWidth(rect.width() - 1);
    rect.setHeight(rect.height() - 1);

    QPainterPath painterPath;
    painterPath.addRoundedRect(rect, 10, 10);
    pt.drawPath(painterPath);

}

}
