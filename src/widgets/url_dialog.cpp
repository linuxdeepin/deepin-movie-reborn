/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "url_dialog.h"
#include "dmr_lineedit.h"
#include "dmr_settings.h"


DWIDGET_USE_NAMESPACE

namespace dmr {
UrlDialog::UrlDialog(QWidget *parent)
    :DDialog(parent)
{
    addButtons(QStringList() << QApplication::translate("UrlDialog", "Cancel")
                                << QApplication::translate("UrlDialog", "Confirm"));
    setOnButtonClickedClose(false);
    setDefaultButton(1);
    setIcon(QIcon(":/resources/icons/logo-big.svg"));
    setMessage(QApplication::translate("UrlDialog", "Please enter the URL:"));

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

