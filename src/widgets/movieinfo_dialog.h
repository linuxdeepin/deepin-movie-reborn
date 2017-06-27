#ifndef _DMR_MOVIE_INFO_DIALOG_H
#define _DMR_MOVIE_INFO_DIALOG_H 

#include <QtWidgets>
#include <ddialog.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
struct MovieInfo;

class MovieInfoDialog: public DDialog {
public:
    MovieInfoDialog(const struct MovieInfo& mi);
};
}

#endif /* ifndef _DMR_MOVIE_INFO_DIALOG_H */
