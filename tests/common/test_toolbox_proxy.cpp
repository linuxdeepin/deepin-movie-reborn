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
#include "src/widgets/tip.h"

using namespace dmr;
/*TEST(ToolBox, buttonBoxButton)
{
    MainWindow* w = dApp->getMainWindow();
    ButtonBoxButton *btn = new ButtonBoxButton("test", w);

    btn->show();
    QTest::qWait(400);
    QTest::mouseMove(btn);

    btn->deleteLater();
}
TEST(ToolBox, buttonTooltip)
{
    MainWindow* w = dApp->getMainWindow();
    ButtonToolTip *tip = new ButtonToolTip(w);

    tip->setText("123");
    tip->show();
    tip->changeTheme(darkTheme);
    tip->show();

    tip->deleteLater();
}
TEST(ToolBox, notificationWidget)
{
    MainWindow* w = dApp->getMainWindow();
    NotificationWidget *nwBottom = new NotificationWidget(w);
    NotificationWidget *nwNone = new NotificationWidget(w);

    nwBottom->setAnchor(NotificationWidget::AnchorBottom);
    nwNone->setAnchor(NotificationWidget::AnchorNone);
    nwBottom->show();
    nwNone->show();
    nwBottom->syncPosition(w->geometry());
    nwNone->syncPosition(w->geometry());
}
TEST(ToolBox, tip)
{
    MainWindow* w = dApp->getMainWindow();
    Tip *tip = new Tip(QPixmap(), "", w);

    tip->setText("test");
    tip->setBackground(QBrush(QColor(Qt::white)));
    tip->setRadius(2);
    tip->setBorderColor(QColor(Qt::blue));
    tip->pop(QPoint(200, 300));
    QColor color = tip->borderColor();
    QBrush brush = tip->background();
    tip->deleteLater();
}*/

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
