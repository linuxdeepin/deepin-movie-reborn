#include "player_engine.h"
#include "burst_screenshots_dialog.h"
#include "dmr_settings.h"
#include "utils.h"

#include <dthememanager.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
BurstScreenshotsDialog::BurstScreenshotsDialog(const PlayItemInfo& pif)
    :DDialog(nullptr)
{
    auto mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(0, 10, 0, 0);
    ml->setSpacing(0);

    setFixedSize(600, 704);

    // top 
    auto *hl = new QHBoxLayout();
    hl->setContentsMargins(0, 0, 0, 0);
    ml->addLayout(hl);

    auto *pm = new QLabel(this);
    pm->setPixmap(QPixmap(":/resources/icons/logo-big.svg").scaled(44, 44));
    hl->setAlignment(pm, Qt::AlignCenter);
    hl->addWidget(pm);

    hl->addSpacing(16);

    // top right up
    auto *trl = new QVBoxLayout;
    trl->setSpacing(0);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addLayout(trl, 1);

    auto *nm = new QLabel(this);
    nm->setText(nm->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 480));
    trl->addWidget(nm);

    //top right bottom
    auto *trb = new QHBoxLayout;
    trb->setContentsMargins(0, 0, 0, 0);
    trb->setSpacing(0);
    {
        auto lb = new QLabel(tr("duration: %1").arg(mi.durationStr()), this);
        lb->setStyleSheet("color: rgba(48, 48, 48, 30%)");
        trb->addWidget(lb);
        trb->addSpacing(36);
    }
    {
        auto lb = new QLabel(tr("resolution: %1").arg(mi.resolution), this);
        lb->setStyleSheet("color: rgba(48, 48, 48, 30%)");
        trb->addWidget(lb);
        trb->addSpacing(36);
    }
    {
        auto lb = new QLabel(tr("size: %1").arg(mi.sizeStr()), this);
        lb->setStyleSheet("color: rgba(48, 48, 48, 30%)");
        trb->addWidget(lb);
        trb->addSpacing(36);
    }
    trb->addStretch(1);
    trl->addLayout(trb);

    ml->addSpacing(20);

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

    _saveBtn = new DTextButton(tr("save"));
    connect(_saveBtn, &QPushButton::clicked, this, &BurstScreenshotsDialog::savePoster);
    _saveBtn->setFixedSize(61, 24);

    QString addition = R"(
    Dtk--Widget--DTextButton {
        line-height: 1;
        font-size: 12px;
        color: #0599ff;
        font-weight: 500;
        text-align: center;

        border: 1px solid rgba(0, 132, 255, 0.4);
        border-radius: 4px;

        outline: none;
        background-color:transparent;
    }


    Dtk--Widget--DTextButton:hover {
        background-color: rgba(0, 132, 255, 0.4);
    }

    Dtk--Widget--DTextButton:pressed {
        background-color: rgba(0, 132, 255, 0.5);
    }
    )";
    auto qss = DThemeManager::instance()->getQssForWidget("DTextButton", "light");
    _saveBtn->setStyleSheet(addition);

    bl->addWidget(_saveBtn);
    ml->addLayout(bl);

    QWidget  *mainContent = new QWidget;
    mainContent->setLayout(ml);
    addContent(mainContent, Qt::AlignCenter);
}

void BurstScreenshotsDialog::updateWithFrames(const QList<QPair<QImage, qint64>>& frames)
{
    int count = 0;
    for (auto frame: frames) {
        auto scaled = frame.first.scaled(178-2, 100-2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        auto *l = new ThumbnailFrame(this);

        int r = count / 3;
        int c = count % 3;

        auto pm = utils::MakeRoundedPixmap({178, 100}, QPixmap::fromImage(scaled), 2, 2, frame.second);
        l->setPixmap(pm);
        _grid->addWidget(l, r, c);
        count++;
    }

    _thumbs = frames;
}

int BurstScreenshotsDialog::exec()
{
    return DDialog::exec();
}

void BurstScreenshotsDialog::savePoster()
{
    auto img = this->grab(rect().marginsRemoved(QMargins(10, 20, 10, 42)));
    _posterPath = Settings::get().screenshotNameTemplate();
    img.save(_posterPath);
    DDialog::accept();
}

void BurstScreenshotsDialog::saveShootings()
{
    int i = 1;
    for (auto& img: _thumbs) {
        auto file_path = Settings::get().screenshotNameSeqTemplate().arg(i++);
        img.first.save(file_path);
    }
    DDialog::accept();
}

QString BurstScreenshotsDialog::savedPosterPath()
{
    return _posterPath;
}

}

