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

    auto *hl = new QHBoxLayout();
    ml->addLayout(hl);

    auto *pm = new QLabel(this);
    pm->setPixmap(QPixmap(":/resources/icons/logo.svg"));
    hl->setAlignment(pm, Qt::AlignCenter);
    hl->addWidget(pm);

    auto *nm = new QLabel(QFileInfo(mi.filePath).fileName(), this);
    hl->setAlignment(nm, Qt::AlignCenter);
    hl->addWidget(nm);

    _grid = new QGridLayout();
    ml->addLayout(_grid);
    _grid->setColumnMinimumWidth(0, 100);
    _grid->setColumnMinimumWidth(1, 100);
    _grid->setColumnMinimumWidth(2, 100);


    QWidget  *mainContent = new QWidget;
    mainContent->setLayout(ml);
    addContent(mainContent, Qt::AlignCenter);

    connect(_engine, &PlayerEngine::notifyScreenshot, this, &BurstScreenshotsDialog::OnScreenshot);
}

void BurstScreenshotsDialog::OnScreenshot(const QPixmap& frame)
{
    qDebug() << __func__ << _count;
    if (frame.isNull()) {
        return;
    }

    auto scaled = frame.scaled(170, 100, Qt::KeepAspectRatio);
    //scaled.save(QString("debug-%1.jpg").arg(QTime::currentTime().msec()));
    auto *l = new QLabel(this);
    int r = _count / 3;
    int c = _count % 3;
    l->setPixmap(scaled);
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

