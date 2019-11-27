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

//#define MV_BASE_INFO tr("Film info")
//#define MV_FILE_TYPE tr("File type")
//#define MV_RESOLUTION tr("Resolution")
//#define MV_FILE_SIZE tr("File size")
//#define MV_DURATION tr("Duration")
//#define MV_FILE_PATH tr("File path")

namespace dmr {
MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo &pif)
    : DAbstractDialog(nullptr)
{
    setFixedSize(300, 441);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setWindowOpacity(0.9);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 10);
    setLayout(layout);

    DImageButton *closeBt = new DImageButton(this);
    closeBt->setFixedSize(50, 50);
    connect(closeBt, &DImageButton::clicked, this, &MovieInfoDialog::close);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);

    const auto &mi = pif.mi;

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
        cover = img.copy(0, (img.height() - sz.height()) / 2, sz.width(), sz.height());
        cover.setDevicePixelRatio(dpr);
    }
    cover = utils::MakeRoundedPixmap(cover, 8, 8);
    pm->setPixmap(cover);
    pm->ensurePolished();
    ml->addWidget(pm);
    ml->setAlignment(pm, Qt::AlignHCenter);
    ml->addSpacing(10);

    auto *nm = new DLabel(this);
    DFontSizeManager::instance()->bind(nm, DFontSizeManager::T8);
    nm->setForegroundRole(DPalette::TextTitle);
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

    auto *form = new QFormLayout(infoRect);
    form->setContentsMargins(10, 5, 0, 30);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignCenter);

#define ADD_ROW(title, field)  do { \
    auto f = new DLabel(title, this); \
    f->setFixedSize(55, 45); \
    f->setAlignment(Qt::AlignLeft | Qt::AlignTop); \
    DFontSizeManager::instance()->bind(f, DFontSizeManager::T8); \
    f->setForegroundRole(DPalette::TextTitle); \
    auto t = new DLabel(field, this); \
    t->setFixedSize(200, 45); \
    t->setAlignment(Qt::AlignLeft | Qt::AlignTop); \
    t->setWordWrap(true); \
    DFontSizeManager::instance()->bind(t, DFontSizeManager::T8); \
    t->setForegroundRole(DPalette::TextTitle); \
    form->addRow(f, t); \
} while (0)

    auto title = new DLabel(tr("Film info"), this);
    DFontSizeManager::instance()->bind(title, DFontSizeManager::T6);
    title->setForegroundRole(DPalette::TextTitle);
    form->addRow(title);

    ADD_ROW(tr("Resolution"), mi.resolution);
    ADD_ROW(tr("File type"), mi.fileType);
    ADD_ROW(tr("File size"), mi.sizeStr());
    ADD_ROW(tr("Duration"), mi.durationStr());

    DLabel *tmp = new DLabel;
    DFontSizeManager::instance()->bind(tmp, DFontSizeManager::T8);
    tmp->setText(mi.filePath);
    auto fm = tmp->fontMetrics();
    auto w = fm.width(mi.filePath);
    if (w > 360) {
        auto fp = utils::ElideText(mi.filePath, {200, 40}, QTextOption::WordWrap,
                                   tmp->font(), Qt::ElideMiddle, fm.height(), 150);
        ADD_ROW(tr("File path"), fp);
    } else {
        ADD_ROW(tr("File path"), mi.filePath);
    }

    delete tmp;
    tmp = nullptr;

#undef ADD_ROW

    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [ = ] {
        nm->setForegroundRole(DPalette::TextTitle);
        title->setForegroundRole(DPalette::TextTitle);
    });

    DPalette pal_this = DApplicationHelper::instance()->palette(this);
    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        pal_this.setBrush(DPalette::Window, QBrush(QColor(248, 248, 248, 0.8)));
        this->setPalette(pal_this);
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
        infoRect->setInfoBgTheme(lightTheme);
    } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        pal_this.setBrush(DPalette::Window, QBrush(QColor(37, 37, 37, 0.8)));
        this->setPalette(pal_this);
        closeBt->setNormalPic(INFO_CLOSE_DARK);
        infoRect->setInfoBgTheme(darkTheme);
    } else {
        pal_this.setBrush(DPalette::Window, QBrush(QColor(248, 248, 248, 0.8)));
        this->setPalette(pal_this);
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
        pt.setBrush(QBrush(QColor(255, 255, 255, 255)));
    } else if (darkTheme == m_themeType) {
        pt.setPen(QColor(255, 255, 255, 20));
        pt.setBrush(QBrush(QColor(45, 45, 45, 250)));
    }

    QRect rect = this->rect();
    rect.setWidth(rect.width() - 1);
    rect.setHeight(rect.height() - 1);

    QPainterPath painterPath;
    painterPath.addRoundedRect(rect, 10, 10);
    pt.drawPath(painterPath);

}

}
