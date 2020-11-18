#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include <gtest/gtest.h>
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "application.h"
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <DSlider>
#include <DListWidget>
#include <QMenu>
#include "presenter.h"
#include "titlebar.h"

using namespace dmr;
TEST(ToolBox, tooltip)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ButtonToolTip *tip = new ButtonToolTip(w);
    tip->setText("123");
    tip->show();
    tip->changeTheme(darkTheme);
    tip->show();

    tip->deleteLater();
}
/*TEST(ToolBox, reloadFile)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    PlayerEngine* egine =  w->engine();

    QList<QUrl> listPlayFiles;

    listPlayFiles<<QUrl::fromLocalFile("/home/uosf/Videos/3.mp4")\
                <<QUrl::fromLocalFile("/home/uosf/Videos/4.mp4")\
               <<QUrl::fromLocalFile("/home/uosf/Music/music1.mp3");

    const auto &valids = egine->addPlayFiles(listPlayFiles);

    egine->playByName(valids[0]);
}*/
