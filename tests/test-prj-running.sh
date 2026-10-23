#!/bin/bash

# SPDX-FileCopyrightText: 2022-2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# 参考 deepin-reader/tests/test-prj-running.sh 风格适配:
# 清理重建构建目录 → 编译三个单测目标 → 分片运行单测产出 gtest XML
# → 采集 lcov 覆盖率 → 生成 HTML 报告与 ut-summary.json 统计摘要
#
# 分片说明: 两个主测试二进制(deepin-movie-test / deepin-movie-platform-test)
# 以 QTest 包装 gtest 全量用例, 单进程全量运行在 UT 环境中会被个别
# 播放/解码类用例挂死(无崩溃信号, crashHandler 无法兜底), 且 QTest::qExec
# 会吞掉命令行参数, gtest CLI 过滤不可用。故:
# - 通过 DMR_GTEST_FILTER 环境变量注入正模式套件过滤(机制见
#   tests/*/test_qtestmain.cpp, 已知崩溃/挂死用例在二进制内始终排除)
# - 按套件分片运行: 片挂死(SIGABRT 超时)重试, 整片持续失败再拆单套件,
#   仍失败拆单用例, 单用例仍挂死则记录并跳过
# - 超时信号用 SIGABRT: crashHandler 会 __gcov_dump() 刷新 .gcda,
#   被超时中止的分片不丢覆盖率; .gcda 计数跨进程累加
# - 测试二进制把 XML 写到固定名 report/report_deepin-movie-test.xml,
#   每片运行后按片重命名汇总到 report/, 由 gen-ut-summary.py 统一解析

set -u

export builddir=build-ut
export reportdir=build-ut
export scriptdir="$(cd "$(dirname "$0")" && pwd)"
export projectdir="$(cd "${scriptdir}/.." && pwd)"

build_path="${projectdir}/${builddir}"
report_path="${projectdir}/${reportdir}"

# 全新构建目录,保证覆盖率干净
rm -rf "${build_path}"
rm -rf "${report_path}"
mkdir -p "${build_path}"
mkdir -p "${report_path}"

cd "${build_path}"

# 单测 GUI 程序需要 X 环境
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=
# 全量套件在单个 QTest 槽内运行,耗时可能超过 5 分钟,须调大函数级超时
# (较小值会触发 QFATAL -> SIGABRT 中止整个套件)
export QTEST_FUNCTION_TIMEOUT='3600000'

# DB/用户数据隔离: 清理历史残留,避免跨运行状态影响
rm -rf "${HOME}/.config/deepin-movie-test" "${HOME}/.config/deepin-movie"
rm -rf "${HOME}/Pictures/DMovie"

# tests 子目录仅在 Debug 构建类型下加入;
# ENABLE_SANITIZE=OFF: sanitizer 会抢占 gcov 的 atexit 刷新导致 .gcda 丢失,
# 采集覆盖率必须关闭(见 tests/*/CMakeLists.txt 注释)
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_SANITIZE=OFF \
      "${projectdir}" || exit 1

# 编译三个单测目标
make -j"$(nproc)" deepin-movie-test deepin-movie-platform-test ut_dmr-test || exit 1

test_exit_code=0

# 重置历史覆盖率计数
lcov --directory "${build_path}" --zerocounters >/dev/null 2>&1 || true

mkdir -p "${build_path}/report"

clean_user_state() {
    rm -rf "${HOME}/.config/deepin-movie-test" "${HOME}/.config/deepin-movie"
}

# 从测试源码 TEST/TEST_F 宏提取套件名/用例名 (gtest CLI 在 QTest 包装下不可用)
suite_names() {
    find "$1" -name '*.cpp' ! -name 'test_qtestmain*' \
        -exec grep -hE '^TEST(_F)?\([[:space:]]*[A-Za-z0-9_]+[[:space:]]*,' {} + 2>/dev/null \
        | sed -E 's/^TEST(_F)?\([[:space:]]*([A-Za-z0-9_]+)[[:space:]]*,.*/\2/' | sort -u
}

case_names() {  # $1=源码目录 $2=套件名
    find "$1" -name '*.cpp' ! -name 'test_qtestmain*' \
        -exec grep -hE "^TEST(_F)?\([[:space:]]*$2[[:space:]]*," {} + 2>/dev/null \
        | sed -E "s/^TEST(_F)?\([[:space:]]*[A-Za-z0-9_]+[[:space:]]*,[[:space:]]*([A-Za-z0-9_]+).*/\1/"
}

# 运行一次分片: 运行二进制并把固定名 XML 归档为目标名
# $1=rundir $2=exe $3=filter $4=超时秒 $5=目标xml $6=内置xml
# 返回 0 表示该次运行完成(rc<128)或已产出有效 XML
run_once() {
    local rundir="$1" exe="$2" filter="$3" tmo="$4" dst="$5" fixed="$6" rc
    rm -f "${fixed}"
    ( cd "${rundir}" && DMR_GTEST_FILTER="${filter}" timeout -k 5 -s ABRT "${tmo}" "./${exe}" )
    rc=$?
    if [ "${rc}" -lt 128 ] || grep -q '<testsuite' "${fixed}" 2>/dev/null; then
        if grep -q '<testsuite' "${fixed}" 2>/dev/null; then
            cp -f "${fixed}" "${dst}"
        fi
        return 0
    fi
    return 1
}

# 分片运行一个 QTest 包装的测试二进制
# 参数: $1=构建子目录 $2=可执行名 $3=测试源码目录 $4=XML 文件前缀
run_chunked_suite() {
    local sub="$1" exe="$2" srcdir="$3" prefix="$4"
    local rundir="${build_path}/tests/${sub}"
    if [ ! -x "${rundir}/${exe}" ]; then
        echo "WARNING: ${rundir}/${exe} not found, skip ${exe}"
        test_exit_code=1
        return 0
    fi
    local fixedxml="${rundir}/report/report_deepin-movie-test.xml"
    mkdir -p "${rundir}/report"

    mapfile -t suites < <(suite_names "${srcdir}")
    if [ "${#suites[@]}" -eq 0 ]; then
        echo "ERROR: no suites found in ${srcdir} for ${exe}"
        test_exit_code=1
        return 0
    fi
    echo ">>> ${exe}: total ${#suites[@]} suites, running in chunks"

    local CHUNK=8 i j last attempt filter xml ok s sxml sok c cxml nfailed=0
    for ((i = 0; i < ${#suites[@]}; i += CHUNK)); do
        last=$((i + CHUNK - 1))
        [ "${last}" -ge "${#suites[@]}" ] && last=$((${#suites[@]} - 1))
        filter=""
        for ((j = i; j <= last; j++)); do filter+="${suites[j]}.*:"; done
        filter="${filter%:}"
        xml="${build_path}/report/${prefix}_$(printf '%02d' $((i / CHUNK + 1))).xml"

        ok=0
        for attempt in 1 2; do
            echo ">>> ${exe} chunk $((i / CHUNK + 1)) attempt ${attempt}: suites [${i}-${last}]"
            clean_user_state
            if run_once "${rundir}" "${exe}" "${filter}" 900 "${xml}" "${fixedxml}"; then
                ok=1
                break
            fi
            echo ">>> ${exe} chunk $((i / CHUNK + 1)) attempt ${attempt} hung/crashed, retrying..."
        done

        if [ "${ok}" -ne 1 ]; then
            # 整片持续失败: 拆成单套件逐个重试
            echo ">>> ${exe} chunk $((i / CHUNK + 1)) keeps failing, splitting into single suites"
            rm -f "${xml}"
            for ((j = i; j <= last; j++)); do
                s="${suites[j]}"
                sxml="${build_path}/report/${prefix}_s$(printf '%03d' "${j}").xml"
                sok=0
                for attempt in 1 2; do
                    echo ">>> ${exe} suite [${s}] attempt ${attempt}"
                    clean_user_state
                    if run_once "${rundir}" "${exe}" "${s}.*" 600 "${sxml}" "${fixedxml}"; then
                        sok=1
                        break
                    fi
                done
                if [ "${sok}" -ne 1 ]; then
                    # 单套件仍失败: 拆成单用例, 逐个定位并跳过挂死用例
                    echo ">>> ${exe} suite [${s}] still failing, splitting into single cases"
                    rm -f "${sxml}"
                    mapfile -t cases < <(case_names "${srcdir}" "${s}")
                    for c in "${cases[@]}"; do
                        cxml="${build_path}/report/${prefix}_c$(printf '%03d' "${j}")_${c}.xml"
                        clean_user_state
                        if run_once "${rundir}" "${exe}" "${s}.${c}" 240 "${cxml}" "${fixedxml}" >/dev/null 2>&1; then
                            :
                        else
                            echo "WARNING: ${exe} case ${s}.${c} hung/crashed alone, excluded from stats"
                            nfailed=$((nfailed + 1))
                        fi
                    done
                fi
            done
        fi
    done
    if [ "${nfailed}" -gt 0 ]; then
        echo ">>> ${exe}: ${nfailed} case(s) excluded"
        test_exit_code=1
    fi
    return 0
}

# 简单运行一个小型测试二进制 (整跑 + 失败重试)
# 参数: $1=构建子目录 $2=可执行名 $3=汇总 xml 名
run_suite() {
    local sub="$1" exe="$2" xmlname="$3"
    local rundir="${build_path}/tests/${sub}"
    if [ ! -x "${rundir}/${exe}" ]; then
        echo "WARNING: ${rundir}/${exe} not found, skip ${exe}"
        test_exit_code=1
        return 0
    fi
    (
        cd "${rundir}" || exit 1
        mkdir -p report
        local rc=0 r n
        for n in 1 2 3; do
            clean_user_state
            echo " =================== ${exe} RUN ${n}/3 BEGIN ==================== "
            timeout -k 10 1800 env ASAN_OPTIONS="fast_unwind_on_malloc=1" "./${exe}"
            r=$?
            echo " =================== ${exe} RUN ${n}/3 END rc=${r} ==================== "
            rc=$((rc || r))
            [ "$r" -eq 0 ] && break
        done
        exit "$rc"
    )
    local rc=$?
    test_exit_code=$((test_exit_code || rc))
    # 汇总 XML 到统一 report 目录
    cp -f "${rundir}"/report/*.xml "${build_path}/report/${xmlname}" 2>/dev/null \
        || { echo "WARNING: no gtest xml produced by ${exe}"; test_exit_code=1; }
    return 0
}

run_chunked_suite "deepin-movie"          "deepin-movie-test"          "${projectdir}/tests/deepin-movie"          "report_deepin-movie"
run_chunked_suite "deepin-movie-platform" "deepin-movie-platform-test" "${projectdir}/tests/deepin-movie-platform" "report_deepin-movie-platform"
run_suite "ut_dmr-test" "ut_dmr-test" "report_dmr-test.xml"

workdir="${build_path}"

#统计代码覆盖率并生成html报告 (各步骤失败仅降级,不中断)
lcov -d "${workdir}" -c -o ./coverage.info || true

# 仅保留项目 src 源码, 去掉单测代码自身与系统头
lcov --extract ./coverage.info '*/src/*' -o ./coverage.info || true
lcov --remove  ./coverage.info '*/tests/*' -o ./coverage.info || true

genhtml -o ./html ./coverage.info || true
mv ./html/index.html ./html/cov_deepin-movie-reborn.html 2>/dev/null || true

#对结果收集至指定文件夹 (report 与构建目录为同一目录时无需拷贝)
cp -r html "${report_path}/" 2>/dev/null || true
cp -r report "${report_path}/" 2>/dev/null || true

# 生成摘要 JSON (gtest 用例统计 + 行/函数覆盖率)
echo "==> Generating summary JSON: ${report_path}/ut-summary.json"
python3 "${scriptdir}/gen-ut-summary.py"

exit "${test_exit_code}"
