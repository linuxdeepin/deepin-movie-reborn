#include <player_widget.h>
#include <compositing_manager.h>
#include <QtWidgets>

int main(int argc, char *argv[])
{
    dmr::CompositingManager::detectOpenGLEarly();
    QApplication app(argc, argv);

    // required by mpv
    setlocale(LC_NUMERIC, "C");
    
    auto mw = new dmr::PlayerWidget;
    mw->setFixedSize(400, 300);
    mw->show();

    if (argc == 2)
        mw->play(QString::fromUtf8(argv[1]));
    
    app.exec();
    return 0;
}
