/*
 * Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef Q_HTTP_SERVER_API
#define Q_HTTP_SERVER_API

#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#ifdef Q_OS_WIN
// Define to export or import depending if we are building or using the library.
// QHTTPSERVER_EXPORT should only be defined when building.
#if defined(QHTTPSERVER_EXPORT)
#define QHTTPSERVER_API __declspec(dllexport)
#else
#define QHTTPSERVER_API __declspec(dllimport)
#endif
#else
// Define empty for other platforms
#define QHTTPSERVER_API
#endif
#else
#ifdef Q_WS_WIN
// Define to export or import depending if we are building or using the library.
// QHTTPSERVER_EXPORT should only be defined when building.
#if defined(QHTTPSERVER_EXPORT)
#define QHTTPSERVER_API __declspec(dllexport)
#else
#define QHTTPSERVER_API __declspec(dllimport)
#endif
#else
// Define empty for other platforms
#define QHTTPSERVER_API
#endif
#endif

#endif
