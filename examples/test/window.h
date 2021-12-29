#ifndef WINDOW_H
#define WINDOW_H

#include <player_widget.h>
#include <player_engine.h>
#include <playlist_model.h>
#include <compositing_manager.h>
#include <QWidget>

class Window: public QWidget {
    Q_OBJECT
public:
    Window(QWidget *parent = 0);
    void play(const QUrl& url);

private:
    dmr::PlayerWidget *player {nullptr};
};

#endif // WINDOW_H
