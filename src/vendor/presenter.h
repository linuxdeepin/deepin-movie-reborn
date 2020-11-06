#ifndef PRESENTER_H
#define PRESENTER_H

#include <QObject>
#include <MprisPlayer>
#include "mainwindow.h"
#include "mpv_proxy.h"
#include "player_engine.h"
#include "dmr_settings.h"
#include "notification_widget.h"
#include "toolbox_proxy.h"
#include "playlist_model.h"

using namespace dmr;

class Presenter : public QObject
{
    Q_OBJECT
public:
    explicit Presenter(MainWindow* mw, QObject *parent = nullptr);

    void initMpris(MprisPlayer *mprisPlayer);

signals:

public slots:
    void slotplay();
    void slotpause();
    void slotplaynext();
    void slotplayprev();
    void slotvolumeRequested(double volume);
    void slotopenUrlRequested(const QUrl url);
    void slotstateChanged();
    void slotloopStatusRequested(Mpris::LoopStatus loopStatus);
    void slotplayModeChanged(PlaylistModel::PlayMode pm);
    void slotvolumeChanged();
    void slotseek(qlonglong Offset);
    void slotstop();
private:
    MainWindow* _mw = nullptr;
    MprisPlayer* m_mprisplayer=nullptr;
};

#endif // PRESENTER_H
