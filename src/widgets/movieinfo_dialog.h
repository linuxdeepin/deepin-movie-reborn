#ifndef _DMR_MOVIE_INFO_DIALOG_H
#define _DMR_MOVIE_INFO_DIALOG_H 

#include <QtWidgets>
#include <ddialog.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
struct PlayItemInfo;

class MovieInfoDialog: public DAbstractDialog {
    Q_OBJECT
public:
    MovieInfoDialog(const struct PlayItemInfo&);
};
}

#endif /* ifndef _DMR_MOVIE_INFO_DIALOG_H */
