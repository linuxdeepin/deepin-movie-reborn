/*
 * Copyright (C) 2016 ~ 2018 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
        m_mainwindow_wayland = new MainWindow();
    return m_mainwindow_wayland;
}

Presenter * Application::initPresenter()
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
