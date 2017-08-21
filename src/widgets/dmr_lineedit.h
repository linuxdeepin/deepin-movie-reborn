#pragma once

#include <QtWidgets>
#include <dlineedit.h>
#include <dimagebutton.h>

DWIDGET_USE_NAMESPACE

namespace dmr {

class LineEdit: public QLineEdit {
public:
    LineEdit(QWidget* p = 0);

protected:
    void showEvent(QShowEvent* se) override;
    void resizeEvent(QResizeEvent* re) override;

private:
    QAction *_clearAct {nullptr};
};
}

