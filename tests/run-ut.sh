#!/bin/bash

# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# deepin-movie-reborn 单测一键入口: 编译 → 分片运行 → lcov 覆盖率 → ut-summary.json
#
# 完整流水线由同目录 test-prj-running.sh 承载(全新构建目录重建、QTest 分片
# 运行 + 挂死重试降级、覆盖率采集与 HTML 报告、摘要生成)。本脚本仅作为
# Agent 外一键运行入口做参数透传:
#   cd <project_root> && ./tests/run-ut.sh
set -u
scriptdir="$(cd "$(dirname "$0")" && pwd)"
exec bash "${scriptdir}/test-prj-running.sh" "$@"
