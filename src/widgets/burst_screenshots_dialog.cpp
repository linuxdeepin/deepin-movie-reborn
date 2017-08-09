#include "player_engine.h"
#include "burst_screenshots_dialog.h"

DWIDGET_USE_NAMESPACE

namespace dmr {
BurstScreenshotsDialog::BurstScreenshotsDialog(PlayerEngine* mpv)
    :DDialog(nullptr), _engine(mpv)
{
    auto mi = mpv->playlist().currentInfo().mi;

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


    QWidget  *mainContent = new QWidget;
    mainContent->setLayout(ml);
    addContent(mainContent, Qt::AlignCenter);

    connect(_engine, &PlayerEngine::notifyScreenshot, this, &BurstScreenshotsDialog::OnScreenshot);
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

void BurstScreenshotsDialog::OnScreenshot(const QImage& frame)
{
    qDebug() << __func__ << _count;
    if (frame.isNull()) {
        return;
    }

    auto scaled = frame.scaled(178, 100, Qt::KeepAspectRatio);
    auto *l = new ThumbnailFrame(this);

    int r = _count / 3;
    int c = _count % 3;

    auto pm = makeRounded(QPixmap::fromImage(scaled));
    l->setPixmap(pm);
    _grid->addWidget(l, r, c);
    _count++;
    if (_count >= 15) {
        _engine->stopBurstScreenshot();
    }
}

int BurstScreenshotsDialog::exec()
{
    _engine->burstScreenshot();
    return DDialog::exec();
}

}

