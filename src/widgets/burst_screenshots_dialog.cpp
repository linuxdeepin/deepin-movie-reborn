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
#include "player_engine.h"
#include "burst_screenshots_dialog.h"
#include "dmr_settings.h"
#include "utils.h"

#include <DThemeManager>

DWIDGET_USE_NAMESPACE

namespace dmr {
BurstScreenshotsDialog::BurstScreenshotsDialog(const PlayItemInfo& pif)
    :DAbstractDialog(nullptr)
{
    auto mi = pif.mi;

    //title
    m_titlebar = new DTitlebar(this);
    m_titlebar->setFixedHeight(50);
    m_titlebar->layout()->setContentsMargins(0, 0, 0, 0);
    m_titlebar->setMenuVisible(false);
    m_titlebar->setIcon(QIcon::fromTheme(":/resources/icons/logo-big.svg"));
    m_titlebar->setFixedWidth(600);
    m_titlebar->setTitle(mi.title);
    m_titlebar->setBackgroundTransparent(true);

    setFixedSize(600, 700);

    //title bottom
    auto *trb = new QHBoxLayout;
    trb->setContentsMargins(0, 0, 0, 0);
    trb->setSpacing(0);
    trb->addStretch(1);
    {
        auto lb = new QLabel(tr("Duration: %1").arg(mi.durationStr()), this);
//        lb->setStyleSheet("color: rgba(48, 48, 48, 60%); font-size: 12px;");
        trb->addWidget(lb);
        trb->addStretch(1);
    }
    {
        auto lb = new QLabel(tr("Resolution: %1").arg(mi.resolution), this);
//        lb->setStyleSheet("color: rgba(48, 48, 48, 60%); font-size: 12px;");
        trb->addWidget(lb);
        trb->addStretch(1);
    }
    {
        auto lb = new QLabel(tr("Size: %1").arg(mi.sizeStr()), this);
//        lb->setStyleSheet("color: rgba(48, 48, 48, 60%); font-size: 12px;");
        trb->addWidget(lb);
        trb->addStretch(1);
    }

    // main content
    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(0, 10, 0, 0);

    _grid = new QGridLayout();
    _grid->setHorizontalSpacing(12);
    _grid->setVerticalSpacing(15);
    _grid->setContentsMargins(0, 0, 0, 0);
    ml->addLayout(_grid);
    _grid->setColumnMinimumWidth(0, 160);
    _grid->setColumnMinimumWidth(1, 160);
    _grid->setColumnMinimumWidth(2, 160);

    auto *bl = new QHBoxLayout;
    bl->setContentsMargins(0, 13, 0, 0);
    bl->addStretch(1);

    _saveBtn = new QPushButton(tr("Save"));
    _saveBtn->setObjectName("SaveBtn");
    connect(_saveBtn, &QPushButton::clicked, this, &BurstScreenshotsDialog::savePoster);
    _saveBtn->setFixedSize(61, 30);

    QString addition = R"(
    QPushButton#SaveBtn {
        font-size: 12px;
        color: #2ca7f8;
        border: 1px solid #2ca7f8;
        border-radius: 4px;
        background: qlineargradient(x1: 0, y1: 0 x2: 0, y2: 1, stop:0 rgba(255, 255, 255, 0.4), stop:1 rgba(253, 253, 253, 0.4));
    }

    QPushButton#SaveBtn:hover {
        background: qlineargradient(x1: 0, y1: 0 x2: 0, y2: 1, stop:0 "#8ccfff", stop:1 "#4bb8ff");
        color: "#fff";
        border: 1px solid rgba(0, 117, 243, 0.2);
    }

    QPushButton#SaveBtn:pressed {
        background: qlineargradient(x1: 0, y1: 0 x2: 0, y2: 1, stop:0 "#0b8cff", stop:1 "#0aa1ff");
        color: "#fff";
        border: 1px solid rgba(29, 129, 255, 0.3);
    }

    QPushButton#SaveBtn:disabled {
        background: qlineargradient(x1: 0, y1: 0 x2: 0, y2: 1, stop:0 rgba(255, 255, 255, 0.4), stop:1 rgba(253, 253, 253, 0.4));
        color: "#AEAEAE";
        border: 1px solid rgba(0,0,0,0.04);
    }
    )";
    _saveBtn->setDefault(true);
//    _saveBtn->setStyleSheet(addition);

    bl->addWidget(_saveBtn);
    ml->addLayout(bl);

//    QWidget  *mainContent = new QWidget;
//    mainContent->setLayout(ml);
//    addContent(mainContent, Qt::AlignCenter);
    QVBoxLayout* mainlayout = new QVBoxLayout;
    mainlayout->setContentsMargins(15, 0, 15, 20);
    mainlayout->setSpacing(0);
    mainlayout->addWidget(m_titlebar);
    mainlayout->addLayout(trb);
    mainlayout->addLayout(ml);

    setLayout(mainlayout);
}

void BurstScreenshotsDialog::updateWithFrames(const QList<QPair<QImage, qint64>>& frames)
{
    auto dpr = qApp->devicePixelRatio();
    QSize sz(178 * dpr, 100 * dpr);
    
    int count = 0;
    for (auto frame: frames) {
        auto scaled = frame.first.scaled(sz.width()-2, sz.height()-2,
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        auto *l = new ThumbnailFrame(this);

        int r = count / 3;
        int c = count % 3;

        auto pm = QPixmap::fromImage(scaled);
        pm.setDevicePixelRatio(dpr);
        pm = utils::MakeRoundedPixmap(sz, pm, 2, 2, frame.second);
        l->setPixmap(pm);
        _grid->addWidget(l, r, c);
        count++;
    }

    _thumbs = frames;
}

int BurstScreenshotsDialog::exec()
{
    return DAbstractDialog::exec();
}

void BurstScreenshotsDialog::savePoster()
{
    auto img = this->grab(rect().marginsRemoved(QMargins(15, 0, 15, 52)));
    _posterPath = Settings::get().screenshotNameTemplate();
    img.save(_posterPath);
    DAbstractDialog::accept();
}

void BurstScreenshotsDialog::saveShootings()
{
    int i = 1;
    for (auto& img: _thumbs) {
        auto file_path = Settings::get().screenshotNameSeqTemplate().arg(i++);
        img.first.save(file_path);
    }
    DAbstractDialog::accept();
}

QString BurstScreenshotsDialog::savedPosterPath()
{
    return _posterPath;
}

}

