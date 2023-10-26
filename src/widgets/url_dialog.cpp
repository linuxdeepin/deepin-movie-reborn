// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "url_dialog.h"
#include "dmr_lineedit.h"
#include "dmr_settings.h"

#include <DGuiApplicationHelper>

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

namespace dmr {
    UrlDialog::UrlDialog(QWidget *parent)
        : DDialog(parent)
    {
        addButtons(QStringList() << QApplication::translate("UrlDialog", "Cancel")
                   << QApplication::translate("UrlDialog", "OK"));
        setOnButtonClickedClose(false);
        setDefaultButton(1);
        setIcon(QIcon::fromTheme("deepin-movie"));

        QLabel* m_messageLabel=new QLabel;
        m_messageLabel->setText(QApplication::translate("UrlDialog", "Please enter the URL:"));
        m_messageLabel->setAlignment(Qt::AlignHCenter|Qt::AlignBottom);
        m_lineEdit = new LineEdit;

        /**
         * m_widget包含一个垂直布局
         * 存放了m_messageLabel和m_lineEdit
         */
        QWidget* m_widget=new QWidget;
        QVBoxLayout* m_contentLayout=new QVBoxLayout;
        m_contentLayout->setSpacing(10);
        m_contentLayout->setContentsMargins({0,0,0,0});
        m_contentLayout->addWidget(m_messageLabel);
        m_contentLayout->addWidget(m_lineEdit);
        m_widget->setLayout(m_contentLayout);
        addContent(m_widget);

        m_lineEdit->setFocusPolicy(Qt::StrongFocus);
        this->setFocusProxy(m_lineEdit);

        if (m_lineEdit->text().isEmpty()) {
            getButton(1)->setEnabled(false);
        }

#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        setFixedSize(251, 150);
    } else {
        setFixedSize(380, 190);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            setFixedSize(380, 190);
        } else {
            setFixedSize(251, 150);
        }
        this->moveToCenter();
    });
#endif

        connect(getButton(0), &QAbstractButton::clicked, this, [ = ] {
            done(QDialog::Rejected);
        });
        connect(getButton(1), &QAbstractButton::clicked, this, [ = ] {
            done(QDialog::Accepted);
        });
        connect(m_lineEdit, &QLineEdit::textChanged, this, &UrlDialog::slotTextchanged);
    }

    QUrl UrlDialog::url() const
    {
        auto u = QUrl(m_lineEdit->text(), QUrl::StrictMode);
        if (u.isLocalFile() || u.scheme().isEmpty())
            return QUrl();

        if (!Settings::get().iscommonPlayableProtocol(u.scheme()))
            return QUrl();

        return u;
    }

    void UrlDialog::showEvent(QShowEvent *se)
    {
        m_lineEdit->setFocus();

        DDialog::showEvent(se);
    }

    void UrlDialog::slotTextchanged()
    {
        if (m_lineEdit->text().isEmpty()) {
            getButton(1)->setEnabled(false);
        } else {
            getButton(1)->setEnabled(true);
        }
    }
}

