#ifndef _DMR_MOVIE_INFO_DIALOG_H
#define _DMR_MOVIE_INFO_DIALOG_H 

#include <QtWidgets>
#include <ddialog.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
struct PlayItemInfo;
class PosterFrame: public QLabel {
    Q_OBJECT
public:
    PosterFrame(QWidget* parent) :QLabel(parent) {
        auto e = new QGraphicsDropShadowEffect(this);
        e->setColor(QColor(0, 0, 0, 255 * 0.2));
        e->setOffset(2, 4);
        e->setBlurRadius(4);
        setGraphicsEffect(e);
    }
};

class MovieInfoDialog: public DAbstractDialog {
    Q_OBJECT
public:
    MovieInfoDialog(const struct PlayItemInfo&);
};
}

#endif /* ifndef _DMR_MOVIE_INFO_DIALOG_H */
