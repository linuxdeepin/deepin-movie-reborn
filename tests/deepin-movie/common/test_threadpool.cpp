// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch1: ThreadPool (src/common/threadpool.cpp)
//
// 用例数: 4   覆盖方法: 5/5 (newThread, moveToNewThread, quitAll, ~ThreadPool, ThreadPool ctor)
// 说明: ThreadPool 构造/析构经 fixture 成员与局部作用域对象隐式覆盖。
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   ThreadPool()                          → 每个用例的 fixture 构造
//   newThread(): new QThread + m_pool.push_back → newThread_returnsDistinctThreadsNotRunning
//   moveToNewThread(): newThread + moveToThread + start → moveToNewThread_movesAndStarts
//   quitAll(): for each thread: quit + wait(2000) → quitAll_stopsAllManagedThreads
//   ~ThreadPool(): quitAll() → destructor_quitsManagedThreads
//
// 隔离说明: ThreadPool::instance() 单例被 harness 的 MainWindow 在启动时使用
// (mainwindow.cpp:1237 moveToNewThread(&m_diskCheckThread))。为不干扰 harness 既有
// 线程池，本测试直接构造独立 ThreadPool 实例(公有构造函数)，不触碰单例状态。

#include <gtest/gtest.h>
#include <QObject>
#include <QThread>

#include "threadpool.h"

/* 类在全局命名空间, 无需 using namespace dmr */

namespace {

class ThreadPoolTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        // 防止用例自身创建的线程跨用例存活；对已结束线程 quit/wait 立即返回。
        m_tp.quitAll();
    }

    ThreadPool m_tp;
};

TEST_F(ThreadPoolTest, newThread_returnsDistinctThreadsNotRunning)
{
    // Arrange
    // (fixture 已构造空的 ThreadPool)

    // Act
    QThread *t1 = m_tp.newThread();
    QThread *t2 = m_tp.newThread();

    // Assert
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);
    EXPECT_NE(t1, t2);
    EXPECT_FALSE(t1->isRunning());
    EXPECT_FALSE(t2->isRunning());
}

TEST_F(ThreadPoolTest, moveToNewThread_movesAndStarts)
{
    // Arrange
    QObject obj;
    QThread *oldThread = obj.thread();

    // Act
    m_tp.moveToNewThread(&obj);
    QThread *newThread = obj.thread();

    // Assert
    EXPECT_NE(newThread, nullptr);
    EXPECT_NE(newThread, oldThread);
    EXPECT_TRUE(newThread->isRunning());

    // 清理: 归还主线程亲和并停止线程
    obj.moveToThread(oldThread);
}

TEST_F(ThreadPoolTest, quitAll_stopsAllManagedThreads)
{
    // Arrange
    QThread *t1 = m_tp.newThread();
    QThread *t2 = m_tp.newThread();
    t1->start();
    t2->start();
    ASSERT_TRUE(t1->isRunning());
    ASSERT_TRUE(t2->isRunning());

    // Act
    m_tp.quitAll();

    // Assert
    EXPECT_FALSE(t1->isRunning());
    EXPECT_FALSE(t2->isRunning());
}

TEST_F(ThreadPoolTest, destructor_quitsManagedThreads)
{
    // Arrange
    QThread *started = nullptr;

    // Act
    {
        ThreadPool local;
        QObject worker;
        local.moveToNewThread(&worker);
        started = worker.thread();
        ASSERT_TRUE(started->isRunning());
    }   // ~ThreadPool → quitAll()

    // Assert
    EXPECT_FALSE(started->isRunning());
}

}  // namespace
