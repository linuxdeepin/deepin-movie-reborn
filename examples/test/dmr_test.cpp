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

        //获取视频信息demo
        bool is = false;
        dmr::MovieInfo mi = player->engine().getMovieInfo(QUrl("file:///usr/share/dde-introduction/demo.mp4"),&is);
        if (is) {
            qInfo() << "# 文件类型:"<< mi.fileType
                    << "# 文件大小:"<< mi.sizeStr()
                    << "# 时长:" << mi.durationStr()
                    << "# 文件路径:"<< mi.filePath
                    << "# 视频流信息:" << mi.videoCodec()
                    << "# 视频码率:"<< mi.vCodeRate << "bps"
                    << "# 视频帧率:"<< mi.fps << "fps"
                    << "# 视频显示比例:" << QString(tr("%1")).arg(static_cast<double>(mi.proportion))
                    << "# 视频分辨率:"<< mi.resolution
                    << "# 音频编码样式:"<< mi.audioCodec()
                    << "# 音频编码码率:"<< mi.aCodeRate << "bps"
                    << "# 音频位数:"<< mi.aDigit << "bits"
                    << "# 声道数:"<< mi.channels << "声道"
                    << "# 采样数:"<< mi.sampling << "hz";
        }
        //获取预览图demo
        QImage img = player->engine().getMovieCover(QUrl("file:///usr/share/dde-introduction/demo.mp4"));
        img.save(QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Desktop/test.png");

        QObject::connect(&player->engine(), &dmr::PlayerEngine::stateChanged, [=]() {
            qInfo() << "----------------new state: " << player->engine().state();
        });
        player->engine().changeVolume(120);

        auto h = new QHBoxLayout(this);

        auto playBtn = new QPushButton("Play");
        connect(playBtn, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::play);
        h->addWidget(playBtn);

        auto pauseBtn = new QPushButton("Pause");
        connect(pauseBtn, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::pauseResume);
        h->addWidget(pauseBtn);

        auto stopBtn = new QPushButton("stop");
        connect(stopBtn, &QPushButton::clicked, &player->engine(), &dmr::PlayerEngine::stop);
        h->addWidget(stopBtn);

        auto forward = new QPushButton("forward");
        connect(forward, &QPushButton::clicked, [=]() {
                player->engine().seekForward(60);
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
        this->player->engine().setBackendProperty("pause-on-start", "true");
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
    dmr::Backend::setDebugLevel(dmr::Backend::DebugLevel::Debug);
    
    auto mw = new Window;
    mw->resize(400, 300);
    mw->show();

    if (argc == 2)
        mw->play(QString::fromUtf8(argv[1]));
    
    app.exec();

    delete mw;
    return 0;
}

#include "dmr_test.moc"
