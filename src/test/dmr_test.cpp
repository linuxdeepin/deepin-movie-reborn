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
#include <player_widget.h>
#include <player_engine.h>
#include <compositing_manager.h>
#include <QtWidgets>

class Window: public QWidget {
    Q_OBJECT
public:
    Window() {

        auto l = new QVBoxLayout(this);

        //if (dmr::CompositingManager::get().composited()) {
            //dmr::CompositingManager::get().overrideCompositeMode(false);
        //}

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
                player->engine().seekForward(90);
        });
        h->addWidget(forward);

        auto volumeUp = new QPushButton("volUp");
        connect(volumeUp, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::volumeUp);
        h->addWidget(volumeUp);

        auto volumeDown = new QPushButton("volDown");
        connect(volumeDown, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::volumeDown);
        h->addWidget(volumeDown);

        auto keep = new QPushButton("KeepOpen");
        connect(keep, &QPushButton::clicked, &player->engine(), [this]() {
                this->player->engine().setBackendProperty("keep-open", "yes");
        });
        h->addWidget(keep);

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
