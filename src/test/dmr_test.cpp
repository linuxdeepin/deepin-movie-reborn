#include <player_widget.h>
#include <player_engine.h>
#include <compositing_manager.h>
#include <QtWidgets>

class Window: public QWidget {
    Q_OBJECT
public:
    Window() {

        auto l = new QVBoxLayout(this);

        if (dmr::CompositingManager::get().composited()) {
            dmr::CompositingManager::get().overrideCompositeMode(false);
        }

        player = new dmr::PlayerWidget;
        l->addWidget(player);

        QObject::connect(&player->engine(), &dmr::PlayerEngine::stateChanged, [=]() {
            qDebug() << "----------------new state: " << player->engine().state();
        });
        player->engine().changeVolume(120);

        auto h = new QHBoxLayout(this);

        auto pause = new QPushButton("Pause");
        connect(pause, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::pauseResume);
        h->addWidget(pause);

        auto stop = new QPushButton("stop");
        connect(stop, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::stop);
        h->addWidget(stop);

        auto forward = new QPushButton("forward");
        connect(forward, &QPushButton::clicked, [=]() {
                player->engine().seekForward(20);
        });
        h->addWidget(forward);

        auto volumeUp = new QPushButton("volUp");
        connect(volumeUp, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::volumeUp);
        h->addWidget(volumeUp);

        auto volumeDown = new QPushButton("volDown");
        connect(volumeDown, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::volumeDown);
        h->addWidget(volumeDown);

        l->addLayout(h);
        setLayout(l);
        
    }

    void play(const QUrl& url) {
        if (player->engine().isPlayableFile(url))
            player->play(url);
    }

private:
    dmr::PlayerWidget *player {nullptr};
};

int main(int argc, char *argv[])
{
    //dmr::CompositingManager::detectOpenGLEarly();
    QApplication app(argc, argv);

    // required by mpv
    setlocale(LC_NUMERIC, "C");
    
    auto mw = new Window;
    mw->resize(400, 300);
    mw->show();

    if (argc == 2)
        mw->play(QString::fromUtf8(argv[1]));
    
    app.exec();
    return 0;
}

#include "dmr_test.moc"
