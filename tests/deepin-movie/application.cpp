/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     fengli <fengli@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#include "application.h"

Application::Application(int &argc, char **argv)
    : DApplication(argc, argv)
{
}

Application::~Application()
{
//    m_mainwindow->close();
//    delete m_mainwindow;
//    m_movieapp->quit();
}

void Application::setMainWindow(MainWindow *window)
{
    if (nullptr != window) {
        m_mainwindow = window;
    }
}
void Application::setMainWindowWayland(MainWindow *window)
{
    if (nullptr != window) {
        m_mainwindow_wayland = window;
    }
}

MainWindow * Application::getMainWindow()
{
    if(nullptr == m_mainwindow)
        m_mainwindow = new MainWindow();
    return m_mainwindow;
}
MainWindow * Application::getMainWindowWayland()
{
    if(nullptr == m_mainwindow_wayland)
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kwayland-shell");
        //qputenv("_d_disableDBusFileDialog", "true");
        setenv("PULSE_PROP_media.role", "video", 1);
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setDefaultFormat(format);
        utils::set_wayland(true);
        bool iswayland = utils::first_check_wayland_env();
        m_mainwindow_wayland = new MainWindow();
    return m_mainwindow_wayland;
}

Presenter * Application::getPresenter()
{
    if(m_presenter == nullptr)
    {
        m_presenter = new Presenter(getMainWindowWayland());
    }
    return m_presenter;
}
MovieApp * Application::initMovieApp(MainWindow *mw)
{
    if(m_movieapp == nullptr)
    {
        m_movieapp = new MovieApp(mw,this);
    }
    return m_movieapp;
}
