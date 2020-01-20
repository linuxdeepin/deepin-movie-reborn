#include "movieapp.h"

MovieApp::MovieApp(MainWindow* mw, QObject* parent)
    :QObject(parent), _mw(mw)
{
    _presenter = new Presenter(_mw);
    initMpris("Deepinmovie");
}

void MovieApp::initMpris(const QString &serviceName)
{
    MprisPlayer* mprisPlayer =  new MprisPlayer();
    mprisPlayer->setServiceName(serviceName);

    //mprisPlayer->setSupportedMimeTypes();
    mprisPlayer->setSupportedUriSchemes(QStringList() << "file");
    mprisPlayer->setCanQuit(true);
    mprisPlayer->setCanRaise(true);
    mprisPlayer->setCanSetFullscreen(false);
    mprisPlayer->setHasTrackList(false);
    // setDesktopEntry: see https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html#Property:DesktopEntry for more
    mprisPlayer->setDesktopEntry("deepin-movie");
    mprisPlayer->setIdentity("Deepin Movie Player");

    mprisPlayer->setCanControl(true);
    mprisPlayer->setCanPlay(true);
    mprisPlayer->setCanGoNext(true);
    mprisPlayer->setCanGoPrevious(true);
    mprisPlayer->setCanPause(true);

    connect(mprisPlayer, &MprisPlayer::quitRequested, this, [ = ]() {
        quit();
    });

    _presenter->initMpris(mprisPlayer);
}

void MovieApp::show()
{

}

void MovieApp::quit()
{
    _mw->requestAction(ActionFactory::Exit);
}
