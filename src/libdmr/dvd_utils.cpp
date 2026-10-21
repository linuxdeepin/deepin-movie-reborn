// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dvd_utils.h"
#include "ddlog.h"

#include <dvdnav/dvdnav.h>

namespace dmr {
//add by xxj
#ifdef heyi
namespace dvd {

/*QString RetrieveDVDTitle(const QString &device)
{
    qCInfo(dmrLib) << "device" << device;
    const char *title = nullptr;

    dvdnav_t *handle = nullptr;
    auto res = dvdnav_open(&handle, device.toUtf8().constData());
    if (res == DVDNAV_STATUS_ERR) {
        qCWarning(dmrLib) << "dvdnav open " << device << "failed";
        return "";
    }

    int32_t nr_titles = 0;
    res = dvdnav_get_number_of_titles(handle, &nr_titles);
    if (res == DVDNAV_STATUS_ERR) {
        goto on_error;
    }

    res = dvdnav_get_title_string(handle, &title);
    if (res == DVDNAV_STATUS_ERR) {
        goto on_error;
    }

#if 0
    uint64_t max_duration = -1;
    QString title = "";
    //uint32_t dvdnav_describe_title_chapters(dvdnav_t *self, int32_t title, uint64_t **times, uint64_t *duration);
    for (int i = 0; i < nr_titles; i++) {
        uint64_t duration = 0;
        auto n = dvdnav_describe_title_chapters(handle, i, NULL, &duration);
        if (max_duration < duration) {
            max_duration = duration;
            //title
        }
    }
#endif
    if (handle) dvdnav_close(handle);
    return QString::fromUtf8(title);

on_error:
    qCWarning(dmrLib) << dvdnav_err_to_string(handle);
    if (handle) dvdnav_close(handle);
    return "";
}*/

static std::atomic<RetrieveDvdThread *> _instance { nullptr };
static QMutex _instLock;
static QMutex _startLock;
static QMutex _runLock;
static QWaitCondition cond;

RetrieveDvdThread::RetrieveDvdThread()
{
    qCDebug(dmrLib) << "Initializing RetrieveDvdThread";
}

RetrieveDvdThread::~RetrieveDvdThread()
{
    qCDebug(dmrLib) << "Destroying RetrieveDvdThread";
    this->requestInterruption();
    this->quit();
    this->wait();
    delete &_instance;
    _instance = nullptr;
}

RetrieveDvdThread *RetrieveDvdThread::get()
{
    if (_instance == nullptr) {
        QMutexLocker lock(&_instLock);
        _instance = new RetrieveDvdThread;
        qCDebug(dmrLib) << "Created new RetrieveDvdThread instance";
    }
    return _instance;
}

void RetrieveDvdThread::startDvd(const QString &dev)
{
    qCDebug(dmrLib) << "Entering RetrieveDvdThread::startDvd() with dev:" << dev;
    qCInfo(dmrLib) << "Starting DVD retrieval for device:" << dev;
    m_dev = dev;
    qCDebug(dmrLib) << "m_dev set to:" << m_dev;
    QMutexLocker lock(&_runLock);
    qCDebug(dmrLib) << "_runLock acquired.";
    start();
    qCDebug(dmrLib) << "RetrieveDvdThread started.";
    qCDebug(dmrLib) << "Exiting RetrieveDvdThread::startDvd()";
}

void RetrieveDvdThread::run()
{
    qCDebug(dmrLib) << "RetrieveDvdThread running";
    setPriority(QThread::IdlePriority);
    qCDebug(dmrLib) << "Thread priority set to IdlePriority.";

    do {
        QMutexLocker lock(&_runLock);
        while (m_dev.isEmpty() && !_quit.load()) {
            cond.wait(lock.mutex(), 40);
        }

        if (_quit.load()) {
            qCDebug(dmrLib) << "RetrieveDvdThread quitting";
            break;
        }

        auto title = getDvdMsg(m_dev);
        qCInfo(dmrLib) << "Retrieved DVD title:" << title;
        emit sigData(title);
    } while (false);

    qCDebug(dmrLib) << "Exiting RetrieveDvdThread::run()";
}

QString RetrieveDvdThread::getDvdMsg(const QString &device)
{
    qCInfo(dmrLib) << "Getting DVD message for device:" << device;
    const char *title = nullptr;
    qCDebug(dmrLib) << "title initialized to nullptr.";

    dvdnav_t *handle = nullptr;
    int32_t res = 0;
#ifndef __mips__
    qCDebug(dmrLib) << "__mips__ is not defined. Attempting to open DVD device.";
    res = dvdnav_open(&handle, device.toUtf8().constData());
    qCDebug(dmrLib) << "dvdnav_open result:" << res;
    if (res == DVDNAV_STATUS_ERR) {
        qCCritical(dmrLib) << "Failed to open DVD device:" << device;
        qCCritical(dmrLib) << "DVD error:" << dvdnav_err_to_string(handle);
        if (handle) dvdnav_close(handle);
        return "dvd open failed";
    }
    qCDebug(dmrLib) << "DVD device opened successfully.";
#else
    qCDebug(dmrLib) << "__mips__ is defined. Skipping dvdnav_open.";
#endif
    int32_t nr_titles = 0;
    qCDebug(dmrLib) << "Attempting to get number of DVD titles.";
    res = dvdnav_get_number_of_titles(handle, &nr_titles);
    qCDebug(dmrLib) << "dvdnav_get_number_of_titles result:" << res;
    if (res == DVDNAV_STATUS_ERR) {
        qCWarning(dmrLib) << "Failed to get number of DVD titles";
        goto on_error;
    }
    qCDebug(dmrLib) << "Number of DVD titles:" << nr_titles;

    qCDebug(dmrLib) << "Attempting to get DVD title string.";
    res = dvdnav_get_title_string(handle, &title);
    qCDebug(dmrLib) << "dvdnav_get_title_string result:" << res;
    if (res == DVDNAV_STATUS_ERR) {
        qCWarning(dmrLib) << "Failed to get DVD title string";
        goto on_error;
    }
    qCDebug(dmrLib) << "DVD title string retrieved:" << QString::fromUtf8(title);

#if 0
    uint64_t max_duration = -1;
    QString title = "";
    //uint32_t dvdnav_describe_title_chapters(dvdnav_t *self, int32_t title, uint64_t **times, uint64_t *duration);
    for (int i = 0; i < nr_titles; i++) {
        uint64_t duration = 0;
        auto n = dvdnav_describe_title_chapters(handle, i, NULL, &duration);
        if (max_duration < duration) {
            max_duration = duration;
            //title
        }
    }
#endif
    if (handle) {
        dvdnav_close(handle);
        qCDebug(dmrLib) << "DVD handle closed successfully";
    }
    QString resultTitle = QString::fromUtf8(title);
    qCDebug(dmrLib) << "Exiting getDvdMsg() with result title:" << resultTitle;
    return resultTitle;

on_error:
    qCWarning(dmrLib) << "DVD error occurred:" << dvdnav_err_to_string(handle);
    if (handle) {
        dvdnav_close(handle);
        qCDebug(dmrLib) << "DVD handle closed after error";
    }
    qCDebug(dmrLib) << "Exiting getDvdMsg() from on_error with empty string.";
    return "";
}

}
#endif
}


