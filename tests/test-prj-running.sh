#!/bin/bash
rm -rf ${HOME}/.config/deepin-movie-test
rm -rf ${HOME}/Pictures/DMovie

rm -rf ../$(dirname $0)/build-ut
rm -rf ../$(dirname $0)/test-deepin-movie
rm -rf ../$(dirname $0)/ut_dmr-test
mkdir ../$(dirname $0)/test-deepin-movie
mkdir ../$(dirname $0)/test-deepin-movie/build-ut
mkdir ../$(dirname $0)/ut_dmr-test
mkdir ../$(dirname $0)/ut_dmr-test/build-ut
mkdir ../$(dirname $0)/build-ut

./deepin-movie/ut-build-run.sh
./ut_dmr-test/ut-build-run.sh

exit 0
