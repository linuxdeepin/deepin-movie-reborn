#!/bin/bash
rm -rf /home/uos/.config/deepin-movie-test

rm -rf ../$(dirname $0)/build-ut
mkdir ../$(dirname $0)/build-ut
cd ../$(dirname $0)/build-ut

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
./tests/$executable
lcov --directory . --capture --output-file ./html/${executable}_Coverage.info

echo " =================== do filter begin ==================== "
lcov --remove ./html/${executable}_Coverage.info 'tests/CMakeFiles/${executable}.dir/${executable}_autogen/*/*' '${executable}_autogen/*/*/*.cpp' '*/usr/include/*' '*/tests/*' '/usr/local/*' '*/src/vendor/dbusextended-qt/*' '*/src/common/filter.*' '*/src/common/settings_translation.cpp' '*/src/common/event_monitor.cpp' '*/src/widgets/videoboxbutton.cpp' '*/src/common/event_relayer.cpp' -o ./html/${executable}_Coverage_fileter.info
echo " ================ do filter end ======================== " 
    
genhtml -o ./html ./html/${executable}_Coverage_fileter.info
    
mv ./html/index.html ./html/cov_${project_name}.html
mv asan.log* asan_${project_name}.log

exit 0
