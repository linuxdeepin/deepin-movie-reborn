#!/bin/bash

rm -rf ../../$(dirname $0)/build-deepin-movie-reborn-unknown-Debug/build-ut
mkdir ../../$(dirname $0)/build-deepin-movie-reborn-unknown-Debug/build-ut
cd ../../$(dirname $0)/build-deepin-movie-reborn-unknown-Debug/tests

export QT_SELECT=qt5
#qmake ../
make test -j4

workdir=$(cd ../../$(dirname $0)/build-deepin-movie-reborn-unknown-Debug/tests; pwd)
#workdir=$(cd ../$(dirname $0)/build-ut; pwd)
executable=deepin-movie-test #可执行程序的文件名

#下面是覆盖率目录操作，一种正向操作，一种逆向操作
extract_info="*/deepin-movie-reborn/*" #针对当前目录进行覆盖率操作
remove_info="*/tests* */src/lib/*" #排除当前目录进行覆盖率操作


build_dir=$workdir
result_coverage_dir=../$build_dir/build-ut/report
result_report_dir=$result_coverage_dir/report_$executable.xml

#$build_dir/$executable --gtest_output=xml:$result_report_dir
$build_dir/$executable -o ./$result_coverage_dir/report_$executable.xml,xml

lcov -d $build_dir -c -o $build_dir/coverage.info

lcov --extract $build_dir/coverage.info $extract_info --output-file  $build_dir/coverage.info
lcov --remove $build_dir/coverage.info $remove_info --output-file $build_dir/coverage.info

lcov --list-full-path -e $build_dir/coverage.info –o $build_dir/coverage-stripped.info

genhtml -o $result_coverage_dir $build_dir/coverage.info

nohup x-www-browser $result_coverage_dir/index.html &
nohup x-www-browser $result_coverage_dir/report_deepin-movie-test.xml &
 
#nohup x-www-browser $result_coverage_dir/index.html &
#nohup x-www-browser $result_report_dir &
 
#lcov -d $build_dir –z

lcov -d $build_dir –z
exit 0
