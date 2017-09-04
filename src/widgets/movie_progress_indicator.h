#ifndef _DMR_MOVIE_PROGRESS_INDICATOR_H
#define _DMR_MOVIE_PROGRESS_INDICATOR_H 

#include <QtWidgets>

namespace dmr {
class MovieProgressIndicator: public QFrame {
    Q_OBJECT
public:
    MovieProgressIndicator(QWidget* parent);

public slots:
    void updateMovieProgress(qint64 duration, qint64 pos);

protected:
    void paintEvent(QPaintEvent* pe) override;

private:
    qint64 _elapsed {0};
    qreal _pert {0.0};
    QSize _fixedSize;
};

}

#endif /* ifndef _DMR_MOVIE_PROGRESS_INDICATOR_H */
