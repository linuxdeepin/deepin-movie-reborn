// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player_widget.h"
#include "filefilter.h"
#include <player_engine.h>
#include <compositing_manager.h>
#include <QDebug>

namespace dmr {

PlayerWidget::PlayerWidget(QWidget *parent)
    : QWidget (parent)
{
    qDebug() << "Entering PlayerWidget::PlayerWidget() constructor.";
    auto forceBind = parent ? parent->property("forceBind") : QVariant(false);
    if (forceBind.isValid() && forceBind.toBool()) {
        qDebug() << "forceBind is valid and true, setting CompositingManager property.";
        CompositingManager::get().setProperty("forceBind", true);
    }
    utils::first_check_wayland_env();
    _engine = new PlayerEngine(this);
    auto *l = new QVBoxLayout;
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(_engine);
    setLayout(l);
    qDebug() << "Exiting PlayerWidget::PlayerWidget() constructor.";
}

PlayerWidget::~PlayerWidget()
{
    qDebug() << "Entering PlayerWidget::~PlayerWidget() destructor.";
    qDebug() << "Exiting PlayerWidget::~PlayerWidget() destructor.";
}

PlayerEngine &PlayerWidget::engine()
{
    return *_engine;
}

void PlayerWidget::play(const QUrl &url)
{
    qDebug() << "Entering PlayerWidget::play() with URL:" << url.toString();
    QUrl realUrl;
    realUrl = FileFilter::instance()->fileTransfer(url.toString());
    qDebug() << "Transferred URL to realUrl:" << realUrl.toString();

    if (!realUrl.isValid()) {
        qDebug() << "Real URL is not valid, returning.";
        return;
    }

    if (!_engine->addPlayFile(realUrl)) {
        qDebug() << "Failed to add play file to engine, returning.";
        return;
    }
    _engine->playByName(realUrl);
    qDebug() << "Exiting PlayerWidget::play().";
}

}
