// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ddlog.h"

#include <DConfig>

// 分类定义：命名必须以 org.deepin. 为前缀，与 deepin-movie.desktop 对应。
// 参考调试模式规范（deepin-debug-config）：warning 默认开启，debug/info 关闭。
Q_LOGGING_CATEGORY(appMovie, "org.deepin.movie")
Q_LOGGING_CATEGORY(dmrLib, "org.deepin.movie.libdmr")
Q_LOGGING_CATEGORY(dmrMpv, "org.deepin.movie.backends.mpv")
Q_LOGGING_CATEGORY(dmrDlna, "org.deepin.movie.dlna")

namespace {

// 用户通过 QT_LOGGING_RULES 环境变量手动设置的规则，始终保留，
// 与 DConfig 下发的规则合并后生效（DConfig 规则优先级靠后，可覆盖同名规则）。
QString envLogRules;

// Qt 限制：setFilterRules 的规则分隔符必须是 \n，不能是 ';'。
QString joinRules(const QString &dconfigRules)
{
    QString rules = envLogRules;
    const QStringList dlist = dconfigRules.split(";");
    for (const QString &item : dlist) {
        const QString rule = item.trimmed();
        if (rule.isEmpty() || rules.contains(rule)) {
            continue;
        }
        rules += rules.isEmpty() ? rule : ("\n" + rule);
    }
    return rules;
}

void applyLogRules(Dtk::Core::DConfig *config)
{
    QLoggingCategory::setFilterRules(joinRules(config->value("log_rules").toString()));
}

}   // namespace

void initLogRulesWatcher()
{
    envLogRules = QString::fromLocal8Bit(qgetenv("QT_LOGGING_RULES"));
    // qunsetenv 之前一定不要有任何日志打印，否则取消环境变量设置不会生效
    qunsetenv("QT_LOGGING_RULES");

    Dtk::Core::DConfig *config = Dtk::Core::DConfig::create("org.deepin.movie", "org.deepin.movie");
    if (!config) {
        return;
    }

    applyLogRules(config);

    // 监听 dconfig 变更，实现运行中动态调整日志级别（调试模式三档切换）
    QObject::connect(config, &Dtk::Core::DConfig::valueChanged, config,
                     [config](const QString &key) {
        if (key == "log_rules") {
            applyLogRules(config);
        }
    });
}
