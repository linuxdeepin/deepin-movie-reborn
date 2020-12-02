#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "dmr_settings.h"
#include "utils.h"
#include "movie_configuration.h"
#include "dbus_adpator.h"

using namespace dmr;
using namespace utils;

TEST(Settings, settings)
{
    Settings::get().isSet(Settings::Flag::ClearWhenQuit);
    Settings::get().isSet(Settings::Flag::ShowThumbnailMode);
    Settings::get().isSet(Settings::Flag::AutoSearchSimilar);
    Settings::get().isSet(Settings::Flag::PreviewOnMouseover);
    Settings::get().isSet(Settings::Flag::MultipleInstance);
    Settings::get().isSet(Settings::Flag::PauseOnMinimize);
    Settings::get().settings()->sync();

    Settings::get().commonPlayableProtocols();
    Settings::get().commonPlayableProtocols();
    Settings::get().iscommonPlayableProtocol("dvb");
    Settings::get().screenshotLocation();
    Settings::get().screenshotNameTemplate();
    Settings::get().screenshotNameSeqTemplate();

    QString path("/usr/share/dde-introduction");
    Settings::get().setThumbnailState();
    Settings::get().setGeneralOption("last_open_path", path);
    QVariant v = Settings::get().generalOption("last_open_path");

    DSettingsOption settingsOption;
    emit settingsOption.valueChanged(v);

    DLineEdit edit;
    emit edit.editingFinished();
}

TEST(Settings, shortcut)
{
    Settings::get().settings()->setOption("shortcuts.play.enable", false);
    Settings::get().settings()->setOption("shortcuts.play.enable", true);
    Settings::get().settings()->setOption("shortcuts.play.playlist", "Shift+Return");
    Settings::get().settings()->setOption("shortcuts.play.movie_info", "Shift+Num+Enter");
    Settings::get().settings()->setOption("subtitle.font.size", 20);
    Settings::get().settings()->setOption("base.play.hwaccel", 1);
    emit Settings::get().hwaccelModeChanged("base.play.hwaccel", 1);
//    Settings::get().settings()->setOption("play.global_volume", 120);
}

TEST(Settings, wayland)
{
    MainWindow *w = dApp->getMainWindow();
    w->close();
    delete w;

    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kwayland-shell");
    //qputenv("_d_disableDBusFileDialog", "true");
    setenv("PULSE_PROP_media.role", "video", 1);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setDefaultFormat(format);
    utils::set_wayland(true);
    bool iswayland = utils::first_check_wayland_env();

    MainWindow *w_wayland = new MainWindow;
//    setlocale(LC_NUMERIC, "C");
    auto &mc = MovieConfiguration::get();
    MovieConfiguration::get().init();
    PlayerEngine *engine =  w_wayland->engine();
    ToolboxProxy *toolboxProxy = w_wayland->toolbox();
    ToolButton *listBtn = toolboxProxy->listBtn();
    ToolButton *fsBtn = toolboxProxy->fsBtn();
    DButtonBoxButton* playBtn = toolboxProxy->playBtn();
    DButtonBoxButton* nextBtn = toolboxProxy->nextBtn();
    DButtonBoxButton* prevBtn = toolboxProxy->prevBtn();
    QList<QUrl> listPlayFiles;
    listPlayFiles << QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                  << QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");
    QTest::qWait(500);
    w_wayland->show();

    Settings::get().settings()->setOption("base.play.showInthumbnailmode", true);
    Settings::get().settings()->setOption("base.play.resumelast", false);

    QTest::mouseMove(playBtn, QPoint(), 300);
    QTest::mouseClick(playBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000); //play

    QTest::qWait(1000);
    const auto &valids = engine->addPlayFiles(listPlayFiles);
    engine->playByName(valids[0]);

    w_wayland->requestAction(ActionFactory::ActionKind::ToggleMute);
    QTest::qWait(500);
    QTest::mouseMove(fsBtn, QPoint(), 500);
    QTest::mouseClick(fsBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);
    QTest::mouseMove(listBtn, QPoint(), 500);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier,QPoint(), 1000);

    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000); //play next
    QTest::qWait(500);
    DGuiApplicationHelper::instance()->setThemeType(DGuiApplicationHelper::DarkType);
    emit DGuiApplicationHelper::instance()->paletteTypeChanged(DGuiApplicationHelper::DarkType);
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000); //play prev

    QTest::mouseMove(listBtn, QPoint(), 500);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);
    QTest::mouseMove(fsBtn, QPoint(), 500);
    QTest::mouseClick(fsBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);

    Settings::get().settings()->setOption("base.play.emptylist", false);

    ApplicationAdaptor *appAdaptor = new ApplicationAdaptor(w_wayland);
    appAdaptor->Raise();
    appAdaptor->openFile("/usr/share/dde-introduction/demo.mp4");
    QStringList fileList;
    fileList << "/usr/share/dde-introduction/demo.mp4"\
             <<"/usr/share/music/bensound-sunny.mp3";
    appAdaptor->openFiles(fileList);


    Settings::get().settings()->setOption("base.play.showInthumbnailmode", false);
    Settings::get().settings()->setOption("base.play.resumelast", true);
//    w_wayland->close();
//    delete w_wayland;
}
