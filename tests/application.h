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
#ifndef APPLICATION_H_
#define APPLICATION_H_

#include <DApplication>
#include "mainwindow.h"
#include "url_dialog.h"
#include "presenter.h"
#include "movieapp.h"

using namespace dmr;

class Application;

#if defined(dApp)
#undef dApp
#endif
#define dApp (static_cast<Application *>(QCoreApplication::instance()))

DWIDGET_USE_NAMESPACE


class Application : public DApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application();

//    bool isRunning();
    //test
    void setMainWindow(MainWindow *window);
    MainWindow *getMainWindow();
    Presenter * initPresenter();
    MovieApp * initMovieApp(MainWindow *mw);

private:
//    bool _isRunning;
    MainWindow *m_mainwindow;
    Presenter *_presenter;
    MovieApp *_movieapp;
};

#endif  // APPLICATION_H_
