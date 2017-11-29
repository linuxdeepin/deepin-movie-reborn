#include "player_widget.h"
#include <player_engine.h>

namespace dmr {

PlayerWidget::PlayerWidget(QWidget *parent)
{
    _engine = new PlayerEngine(this);
    auto *l = new QVBoxLayout;
    l->addWidget(_engine);
    setLayout(l);
}

PlayerWidget::~PlayerWidget() 
{
}

PlayerEngine& PlayerWidget::engine()
{
    return *_engine;
}

void PlayerWidget::play(const QUrl& url)
{
    if (!url.isValid()) 
        return;

    if (!_engine->addPlayFile(url)) {
        return;
    }
    _engine->playByName(url);
}

}
