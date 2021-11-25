#!/bin/bash
rm -rf ${HOME}/.config/deepin-movie-test
rm -rf ${HOME}/Pictures/DMovie

rm -rf ../$(dirname $0)/build-ut
mkdir ../$(dirname $0)/build-ut
cd ../$(dirname $0)/build-ut

platform=`uname -m`
echo ${platform}

export DISPLAY=":0"
export QT_QPA_PLATFORM=
echo $QT_QPA_PLATFORM
export QTEST_FUNCTION_TIMEOUT='500000'
#export QT_SELECT=qt5
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j16

workdir=$(cd ../$(dirname $0)/build-ut; pwd)
executable=deepin-movie-test #可执行程序的文件名
project_name=deepin-movie-reborn

mkdir -p html
mkdir -p report

echo " ===================CREAT LCOV REPROT==================== "
lcov --directory ./tests/CMakeFiles/deepin-movie-test.dir --zerocounters
ASAN_OPTIONS="fast_unwind_on_malloc=1" ./tests/$executable
lcov --directory . --capture --output-file ./html/${executable}_Coverage.info

echo " =================== do filter begin ==================== "
if [ ${platform} = x86_64 ];then 
lcov --remove ./html/${executable}_Coverage.info 'tests/CMakeFiles/${executable}.dir/${executable}_autogen/*/*' '${executable}_autogen/*/*/*.cpp' '*/usr/include/*' '*/tests/*' '/usr/local/*' '*/src/common/utility_x11.*' '*/src/common/settings_translation.cpp' '*/src/common/event_monitor.cpp' '*/src/widgets/videoboxbutton.cpp' '*/src/common/hwdec_probe.*' -o ./html/${executable}_Coverage_fileter.info
else
lcov --remove ./html/${executable}_Coverage.info 'tests/CMakeFiles/${executable}.dir/${executable}_autogen/*/*' '${executable}_autogen/*/*/*.cpp' '*/usr/include/*' '*/tests/*' '/usr/local/*' '*/src/common/utility_x11.*' '*/src/common/settings_translation.cpp' '*/src/common/event_monitor.cpp' '*/src/widgets/videoboxbutton.cpp' '*/src/backends/mpv/mpv_glwidget.cpp' '*/src/common/thumbnail_worker.*' -o ./html/${executable}_Coverage_fileter.info
echo true
fi
echo " =================== do filter end ====================== "
    
genhtml -o ./html ./html/${executable}_Coverage_fileter.info
    
mv ./html/index.html ./html/cov_${project_name}.html
mv asan.log* asan_${project_name}.log

ls report/

exit 0
