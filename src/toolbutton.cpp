#include "toolbutton.h"
#include <dthememanager.h>
#include <DApplication>

namespace dmr {
ToolButton::ToolButton(const QString& name, QWidget* parent)
    : DImageButton(parent), _name{name}
{
    connect(DThemeManager::instance(), &DThemeManager::themeChanged,
            this, &ToolButton::onThemeChanged);

}

void ToolButton::onThemeChanged()
{
    qDebug() << "theme " << qApp->theme();
    QFile darkF(":/resources/qss/dark/widgets.qss"),
          lightF(":/resources/qss/light/widgets.qss");

    if ("dark" == qApp->theme()) {
        if (darkF.open(QIODevice::ReadOnly)) {
            setStyleSheet(darkF.readAll());
            darkF.close();
        } else {
            qDebug() << "Set dark style sheet for ImageButton failed";
        }
    } else {
        if (lightF.open(QIODevice::ReadOnly)) {
            setStyleSheet(lightF.readAll());
            lightF.close();
        } else {
            qDebug() << "Set light style sheet for ImageButton failed";
        }
    }
}

}

