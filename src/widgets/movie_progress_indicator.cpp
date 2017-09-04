#include "movie_progress_indicator.h"


namespace dmr {

MovieProgressIndicator::MovieProgressIndicator(QWidget* parent)
    :QFrame(parent)
{
    QFont ft;
    ft.setPixelSize(14);
    ft.setWeight(500);
    QFontMetrics fm(ft);

    _fixedSize = QSize(qMax(52, fm.width("99:99:99")), fm.height() + 10);
    this->setFixedSize(_fixedSize);
}

void MovieProgressIndicator::paintEvent(QPaintEvent* pe)
{
    QTime t(0, 0, 0);
    t = t.addSecs(_elapsed);
    auto time_text = t.toString("hh:mm:ss");

    QPainter p(this);
    QFont ft;
    ft.setPixelSize(14);
    ft.setWeight(500);
    p.setFont(ft);
    p.setPen(QColor(255, 255, 255, 255 * .4));

    auto fm = p.fontMetrics();
    auto fr = fm.boundingRect(time_text);
    fr.moveCenter(QPoint(rect().center().x(), fr.center().y()));
    fr.moveCenter(rect().center());
    p.drawText(fr, time_text);

    
    QPoint pos((_fixedSize.width() - 48)/2, rect().height() - 5);
    int pert = qMin(_pert * 10, 10.0);
    for (int i = 0; i < 10; i++) {
        if (i >= pert) {
            p.fillRect(QRect(pos, QSize(3, 3)), QColor(255, 255, 255, 255 * .25));
        } else {
            p.fillRect(QRect(pos, QSize(3, 3)), QColor(255, 255, 255, 255 * .5));
        }
        pos.rx() += 5;
    }
}

void MovieProgressIndicator::updateMovieProgress(qint64 duration, qint64 pos)
{
    _elapsed = pos;
    _pert = (qreal)pos / duration;
    update();
}

}
