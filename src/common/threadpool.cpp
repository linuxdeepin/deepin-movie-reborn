// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threadpool.h"
#include "ddlog.h"

#include <QDebug>

ThreadPool::ThreadPool(QObject *parent) : QObject(parent)
{
    qCDebug(appMovie) << "Entering ThreadPool constructor.";
    qCDebug(appMovie) << "Exiting ThreadPool constructor.";
}

ThreadPool::~ThreadPool()
{
    qCDebug(appMovie) << "Entering ThreadPool destructor.";
    quitAll();
    qCDebug(appMovie) << "Exiting ThreadPool destructor. quitAll() called.";
}

QThread *ThreadPool::newThread()
{
    qCDebug(appMovie) << "Entering ThreadPool::newThread().";
    auto thread = new QThread;
//    qCInfo(appMovie) << "add <<<<<<<" << thread;
    m_pool.push_back(thread);

    qCDebug(appMovie) << "Exiting ThreadPool::newThread(). Returning new thread.";
    return thread;
}

void ThreadPool::moveToNewThread(QObject *obj)
{
    auto work = newThread();
    obj->moveToThread(work);
    work->start();
}

void ThreadPool::quitAll()
{
    for (auto thread : m_pool) {
//        qCInfo(appMovie) << thread;
        thread->quit();
        thread->wait(2000);
    }
    qCInfo(appMovie) << "all thread quit";
}
