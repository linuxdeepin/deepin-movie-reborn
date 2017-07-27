#pragma once

#include <QtWidgets>
#include <ddialog.h>

#include <dlineedit.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
class UrlDialog: public DDialog {
public:
    UrlDialog();
    QUrl url() const;

private:
    DLineEdit* _le {nullptr};
};
}
