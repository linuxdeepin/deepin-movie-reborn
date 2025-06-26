// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QLibrary>
#include <QDir>
#include <QLibraryInfo>
#include <QJsonDocument>
#include <QDebug>

#include "eventlogutils.h"

EventLogUtils *EventLogUtils::m_instance(nullptr);

EventLogUtils &EventLogUtils::get()
{
    if (m_instance == nullptr) {
        qDebug() << "m_instance is nullptr. Creating new EventLogUtils instance.";
        m_instance = new EventLogUtils;
    }
    return *m_instance;
}

EventLogUtils::EventLogUtils()
{
    qDebug() << "Entering EventLogUtils::EventLogUtils() constructor.";
    QLibrary library("libdeepin-event-log.so");
    qDebug() << "Loaded QLibrary: libdeepin-event-log.so";

    init =reinterpret_cast<bool (*)(const std::string &, bool)>(library.resolve("Initialize"));
    qDebug() << "Resolved 'Initialize' function. init pointer:" << (init != nullptr);
    writeEventLog = reinterpret_cast<void (*)(const std::string &)>(library.resolve("WriteEventLog"));
    qDebug() << "Resolved 'WriteEventLog' function. writeEventLog pointer:" << (writeEventLog != nullptr);

    if (init == nullptr) {
        qDebug() << "Initialize function not resolved. Exiting constructor.";
        return;
    }

    qDebug() << "Calling Initialize for deepin-movie.";
    init("deepin-movie", true);
    qDebug() << "Exiting EventLogUtils::EventLogUtils() constructor.";
}

void EventLogUtils::writeLogs(QJsonObject &data)
{
    qDebug() << "Entering EventLogUtils::writeLogs() with data.";
    if (writeEventLog == nullptr) {
        qDebug() << "writeEventLog is nullptr. Exiting writeLogs().";
        return;
    }

    //std::string str = QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString();
    qDebug() << "Converting QJsonObject to compact JSON string and writing event log.";
    writeEventLog(QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString());
    qDebug() << "Exiting EventLogUtils::writeLogs().";
}
