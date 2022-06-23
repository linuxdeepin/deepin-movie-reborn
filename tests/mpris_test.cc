#include <mprisplayer.h>

int main(int argc, char *argv[])
{
    MprisPlayer *mprisPlayer = new MprisPlayer();
    // deepin fork mpris
    mprisPlayer->setCanShowInUI(false);
    return 0;
}
