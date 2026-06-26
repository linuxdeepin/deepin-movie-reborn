#!/bin/bash

# SPDX-FileCopyrightText: 2022-2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

#workdir=$(cd ../$(dirname $0)/deepin-movie/build-ut; pwd)
executable=deepin-movie-test #可执行程序的文件名

platform=`uname -m`
echo ${platform}

cd ./tests/deepin-movie/

mkdir -p html
mkdir -p report

echo " ===================CREAT LCOV REPROT==================== "
lcov --directory ./CMakeFiles/deepin-movie-test.dir --zerocounters
# 跑 3 次累加 .gcda：deepin-movie-test 存在 flaky 跨套件崩溃，但 test_qtestmain
# 的 crashHandler 会在崩溃时 __gcov_dump() 刷新 .gcda，所以崩溃的那次仍贡献到
# 崩溃点为止的覆盖；多次累加把 flaky 崩溃丢掉的尾部补回来。
for ut_run in 1 2 3; do
    ASAN_OPTIONS="fast_unwind_on_malloc=1" ./$executable; ec=$?; echo "==>> ${executable} 第${ut_run}次 退出码=$ec (134=SIGABRT/Q_ASSERT, 139=SIGSEGV, 0=正常)"
done
lcov --directory . --capture --output-file ./html/${executable}_Coverage.info

echo " =================== do filter begin ==================== "
if [ ${platform} = x86_64 ];then
lcov --remove ./html/${executable}_Coverage.info 'tests/CMakeFiles/${executable}.dir/${executable}_autogen/*/*' '${executable}_autogen/*/*/*.cpp' '*/usr/include/*' '*/tests/*' '/usr/local/*' '*/src/common/utility_x11.*' '*/src/common/settings_translation.cpp' '*/src/common/event_monitor.cpp' '*/src/widgets/videoboxbutton.cpp' '*/src/common/hwdec_probe.*'  '*/src/common/platform/*' '*/src/widgets/platform/*' '*/src/dlna/dlnaHttpServer/*'  -o ./html/${executable}_Coverage_fileter.info
else
lcov --remove ./html/${executable}_Coverage.info 'tests/CMakeFiles/${executable}.dir/${executable}_autogen/*/*' '${executable}_autogen/*/*/*.cpp' '*/usr/include/*' '*/tests/*' '/usr/local/*' '*/src/common/utility_x11.*' '*/src/common/settings_translation.cpp' '*/src/common/event_monitor.cpp' '*/src/widgets/videoboxbutton.cpp' '*/src/backends/mpv/mpv_glwidget.cpp' '*/src/common/thumbnail_worker.*'  '*/src/common/platform/*' '*/src/widgets/platform/*' '*/src/dlna/dlnaHttpServer/*' -o ./html/${executable}_Coverage_fileter.info
echo true
fi
echo " =================== do filter end ====================== "
    
genhtml -o ./html ./html/${executable}_Coverage_fileter.info
    
mv ./html/index.html ./html/cov_${executable}.html
mv asan.log* asan_${executable}.log

cp -r ./html/ ../../
cp -r ./report/ ../../
cp ./asan_${executable}.log ../../

#ls report/

exit 0
