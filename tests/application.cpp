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
    m_mainwindow->close();
}

//bool Application::isRunning()
//{
//    return _isRunning;
//}

void Application::setMainWindow(MainWindow *window)
{
    if (nullptr != window) {
        m_mainwindow = window;
    }
}

MainWindow * Application::getMainWindow()
{
//    if(nullptr == m_mainwindow)
//        m_mainwindow = new MainWindow();
    return m_mainwindow;
}

Presenter * Application::initPresenter()
{

    _presenter = new Presenter(getMainWindow());
    return _presenter;
}
MovieApp * Application::initMovieApp(MainWindow *mw)
{
    _movieapp = new MovieApp(mw,this);
    return _movieapp;
}
