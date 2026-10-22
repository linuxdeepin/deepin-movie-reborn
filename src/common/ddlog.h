// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DDLOG_H
#define DDLOG_H

#include <QDebug>
#include <QLoggingCategory>

// 全项目统一的日志分类：分类名以 org.deepin.movie 为前缀，与 deepin-movie.desktop
// 对应，便于 DConfig（调试模式 / deepin-log-viewer）按应用统一过滤。
// 各运行单元（主程序 / libdmr 共享库）与子模块用 "." 连接进一步细分，
// shell 侧规则使用通配符（如 *.movie*.debug=true）统一控制。

// 主程序（deepin-movie：main / common / widgets / vendor）
Q_DECLARE_LOGGING_CATEGORY(appMovie)

// libdmr 共享库（libdmr.so，可被其他程序链接加载）
Q_DECLARE_LOGGING_CATEGORY(dmrLib)

// mpv 后端（同时编入主程序与 libdmr）
Q_DECLARE_LOGGING_CATEGORY(dmrMpv)

// DLNA 投屏模块
Q_DECLARE_LOGGING_CATEGORY(dmrDlna)

// 注册 DConfig(org.deepin.movie) 的 log_rules 动态日志规则监听。
// 必须在 main() 最开始、任何日志打印与 Application 实例化之前调用，
// 否则取消 QT_LOGGING_RULES 环境变量的操作不会生效。
void initLogRulesWatcher();

#endif   // DDLOG_H
