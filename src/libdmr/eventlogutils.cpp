// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QLibrary>
#include "ddlog.h"
#include <QDir>
#include <QLibraryInfo>
#include <QJsonDocument>
#include <QDebug>

#include "eventlogutils.h"

EventLogUtils *EventLogUtils::m_instance(nullptr);

EventLogUtils &EventLogUtils::get()
{
    if (m_instance == nullptr) {
        qCDebug(dmrLib) << "m_instance is nullptr. Creating new EventLogUtils instance.";
        m_instance = new EventLogUtils;
    }
    return *m_instance;
}

EventLogUtils::EventLogUtils()
{
    qCDebug(dmrLib) << "Entering EventLogUtils::EventLogUtils() constructor.";
    QLibrary library("libdeepin-event-log.so");
    qCDebug(dmrLib) << "Loaded QLibrary: libdeepin-event-log.so";

    init =reinterpret_cast<bool (*)(const std::string &, bool)>(library.resolve("Initialize"));
    qCDebug(dmrLib) << "Resolved 'Initialize' function. init pointer:" << (init != nullptr);
    writeEventLog = reinterpret_cast<void (*)(const std::string &)>(library.resolve("WriteEventLog"));
    qCDebug(dmrLib) << "Resolved 'WriteEventLog' function. writeEventLog pointer:" << (writeEventLog != nullptr);

    if (init == nullptr) {
        qCDebug(dmrLib) << "Initialize function not resolved. Exiting constructor.";
        return;
    }

    qCDebug(dmrLib) << "Calling Initialize for deepin-movie.";
    init("deepin-movie", true);
    qCDebug(dmrLib) << "Exiting EventLogUtils::EventLogUtils() constructor.";
}

void EventLogUtils::writeLogs(QJsonObject &data)
{
    qCDebug(dmrLib) << "Entering EventLogUtils::writeLogs() with data.";
    if (writeEventLog == nullptr) {
        qCDebug(dmrLib) << "writeEventLog is nullptr. Exiting writeLogs().";
        return;
    }

    //std::string str = QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString();
    qCDebug(dmrLib) << "Converting QJsonObject to compact JSON string and writing event log.";
    writeEventLog(QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString());
    qCDebug(dmrLib) << "Exiting EventLogUtils::writeLogs().";
}
