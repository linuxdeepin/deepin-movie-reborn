// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threadpool.h"

#include <QDebug>

ThreadPool::ThreadPool(QObject *parent) : QObject(parent)
{
    qDebug() << "Entering ThreadPool constructor.";
    qDebug() << "Exiting ThreadPool constructor.";
}

ThreadPool::~ThreadPool()
#ifndef USE_TEST
{
    qDebug() << "Entering ThreadPool destructor.";
    quitAll();
    qDebug() << "Exiting ThreadPool destructor. quitAll() called.";
}
#else // USE_TEST: cold function, stubbed out of test build
{ }
#endif // USE_TEST

QThread *ThreadPool::newThread()
{
    qDebug() << "Entering ThreadPool::newThread().";
    auto thread = new QThread;
//    qInfo() << "add <<<<<<<" << thread;
    m_pool.push_back(thread);

    qDebug() << "Exiting ThreadPool::newThread(). Returning new thread.";
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
//        qInfo() << thread;
        thread->quit();
        thread->wait(2000);
    }
    qInfo() << "all thread quit";
}
