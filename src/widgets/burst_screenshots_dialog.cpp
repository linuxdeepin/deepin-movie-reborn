#include "player_engine.h"
#include "burst_screenshots_dialog.h"

#include <dthememanager.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
BurstScreenshotsDialog::BurstScreenshotsDialog(const PlayItemInfo& pif)
    :DDialog(nullptr)
{
    auto mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(0, 0, 0, 0);

    // top 
    auto *hl = new QHBoxLayout();
    hl->setSpacing(16);
    ml->addLayout(hl);

    auto *pm = new QLabel(this);
    pm->setPixmap(QPixmap(":/resources/icons/logo-big.svg").scaled(44, 44));
    hl->setAlignment(pm, Qt::AlignCenter);
    hl->addWidget(pm);

    // top right up
    auto *trl = new QVBoxLayout;
    hl->setContentsMargins(21, 0, 0, 0);
    hl->setSpacing(6);
    hl->addLayout(trl, 1);

    auto *nm = new QLabel(QFileInfo(mi.filePath).fileName(), this);
    trl->addWidget(nm);

    //top right bottom
    auto *trb = new QGridLayout;
    trb->addWidget(new QLabel(tr("duration: %1").arg(mi.durationStr()), this), 0, 0);
    trb->addWidget(new QLabel(tr("resolution: %1").arg(mi.resolution), this), 0, 1);
    trb->addWidget(new QLabel(tr("size: %1").arg(mi.sizeStr()), this), 0, 2);
    trl->addLayout(trb);



    _grid = new QGridLayout();
    _grid->setHorizontalSpacing(12);
    _grid->setVerticalSpacing(15);
    _grid->setContentsMargins(21, 0, 0, 0);
    ml->addLayout(_grid);
    _grid->setColumnMinimumWidth(0, 160);
    _grid->setColumnMinimumWidth(1, 160);
    _grid->setColumnMinimumWidth(2, 160);

    auto *bl = new QHBoxLayout;
    bl->addStretch(1);

    _saveBtn = new DTextButton(tr("save"));
    _saveBtn->setFixedSize(61, 24);

    QString addition = R"(
    Dtk--Widget--DTextButton {
        line-height: 1;
        font-size: 12px;
        color: #0599ff;
    }
    )";
    auto qss = DThemeManager::instance()->getQssForWidget("DTextButton", "light");
    _saveBtn->setStyleSheet(qss + addition);
    bl->addWidget(_saveBtn);
    ml->addLayout(bl);

    QWidget  *mainContent = new QWidget;
    mainContent->setLayout(ml);
    addContent(mainContent, Qt::AlignCenter);
}

static QPixmap makeRounded(QPixmap pm)
{
    QPixmap dest(pm.size());

    QPainter p(&dest);
    p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRoundedRect(QRect(QPoint(), pm.size()), 2, 2);
    p.setClipPath(path);

    p.drawPixmap(0, 0, pm);

    return dest;
}

void BurstScreenshotsDialog::updateWithFrames(const QList<QImage>& frames)
{
    int count = 0;
    for (auto frame: frames) {
        auto scaled = frame.scaled(178, 100, Qt::KeepAspectRatio);
        auto *l = new ThumbnailFrame(this);

        int r = count / 3;
        int c = count % 3;

        auto pm = makeRounded(QPixmap::fromImage(scaled));
        l->setPixmap(pm);
        _grid->addWidget(l, r, c);
        count++;
    }
}

int BurstScreenshotsDialog::exec()
{
    return DDialog::exec();
}

}

