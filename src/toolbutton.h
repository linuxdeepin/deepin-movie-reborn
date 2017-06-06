#ifndef _DMR_TOOLBUTTON_H
#define _DMR_TOOLBUTTON_H 

#include <QtWidgets>
#include <dimagebutton.h>

DWIDGET_USE_NAMESPACE

namespace dmr {

class ToolButton: public DImageButton {
    Q_OBJECT
public:
    // name is the base, all states build from this, such as name-press, name-hover etc
    ToolButton(const QString& name, QWidget* parent = 0);

protected slots:
    void onThemeChanged();

private:
    QString _name;
};

}

#endif /* ifndef _DMR_TOOLBUTTON_H */

