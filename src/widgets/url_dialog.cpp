#include "url_dialog.h"
#include "dmr_lineedit.h"
#include "dmr_settings.h"


DWIDGET_USE_NAMESPACE

namespace dmr {
UrlDialog::UrlDialog()
    :DDialog(nullptr)
{
    addButtons(QStringList() << QApplication::translate("UrlDialog", "Cancel")
                                << QApplication::translate("UrlDialog", "Confirm"));
    setOnButtonClickedClose(false);
    setDefaultButton(1);
    setIcon(QIcon(":/resources/icons/logo-big.svg"));
    setMessage(QApplication::translate("UrlDialog", "Please input the url of file to play"));

    _le = new LineEdit;
    addContent(_le);


    connect(getButton(0), &QAbstractButton::clicked, this, [=] {
        done(QDialog::Rejected);
    });
    connect(getButton(1), &QAbstractButton::clicked, this, [=] {
        done(QDialog::Accepted);
    });

    _le->setFocusPolicy(Qt::StrongFocus);
    this->setFocusProxy(_le);
}

QUrl UrlDialog::url() const
{
    auto u = QUrl(_le->text(), QUrl::StrictMode);
    if (u.isLocalFile() || u.scheme().isEmpty()) 
        return QUrl();

    if (!Settings::get().iscommonPlayableProtocol(u.scheme()))
        return QUrl();

    return u;
}

void UrlDialog::showEvent(QShowEvent* se)
{
    _le->setFocus();
}

}

