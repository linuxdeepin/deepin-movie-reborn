#pragma once

#include <QtWidgets>

namespace dmr {
class PlayerEngine;

class PlayerWidget: public QWidget {
    Q_OBJECT
public:
    PlayerWidget(QWidget *parent = 0);
    virtual ~PlayerWidget();

    /**
     * engine is instantiated in constructor, and all interaction comes from
     * engine
     */
    PlayerEngine& engine();
    void play(const QUrl& url);

protected:
    PlayerEngine *_engine {nullptr};
};
}

