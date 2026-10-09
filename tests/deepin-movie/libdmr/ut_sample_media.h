// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 测试辅助：运行时生成样例媒体文件，供 libdmr 各测试文件复用。
// 使用 QTemporaryFile 生成唯一临时路径，避免硬编码 /tmp 路径带来的
// 符号链接攻击风险与环境依赖。
#ifndef UT_SAMPLE_MEDIA_H
#define UT_SAMPLE_MEDIA_H

#include <QString>
#include <QFile>
#include <QTemporaryFile>
#include <QProcess>

// 生成 1 秒 128x96 的 H.264 测试视频，返回唯一临时文件路径。
// 进程内仅生成一次；ffmpeg 不可用或执行失败时返回空路径，
// 由调用方通过 QFileInfo::exists 判断后决定是否 GTEST_SKIP。
inline QString ut_ensureSampleMedia()
{
    static QString path;
    if (path.isEmpty()) {
        QTemporaryFile tmp(QStringLiteral("/tmp/ut_sample_XXXXXX.mp4"));
        tmp.setAutoRemove(false);
        if (!tmp.open())
            return QString();
        path = tmp.fileName();
        tmp.close();
        const int ret = QProcess::execute(
            QStringLiteral("ffmpeg"),
            {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
             QStringLiteral("-i"), QStringLiteral("testsrc=duration=1:size=128x96:rate=10"),
             QStringLiteral("-c:v"), QStringLiteral("libx264"),
             QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path});
        if (ret != 0) {
            QFile::remove(path);
            path.clear();
        }
    }
    return path;
}

#endif // UT_SAMPLE_MEDIA_H
