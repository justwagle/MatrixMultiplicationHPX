#!/bin/bash
set -x
set -e

if [[ ! $PARALLEL_BUILD ]]; then
    echo "AutoTuneTMP: PARALLEL_BUILD not set, defaulting to 4"
    export PARALLEL_BUILD=4
fi

if [ ! -d "AutoTuneTMP" ]; then
    git clone git@github.com:DavidPfander-UniStuttgart/AutoTuneTMP.git
    cd AutoTuneTMP
    git submodule init
    git submodule update
    cd ..
fi

cd AutoTuneTMP
git pull
cd ..
# else
#     cd hpx
#     git pull
#     cd ..
# fi

mkdir -p AutoTuneTMP/build
cd AutoTuneTMP/build

echo "compiling AutoTuneTMP"
# detection of Vc doesn't work with a relative path
cmake -DCMAKE_C_COMPILER=gcc-7 -DCMAKE_CXX_COMPILER=g++-7 -DVc_ROOT="$Vc_ROOT" -DCMAKE_BUILD_TYPE=release ../ > cmake_AutoTuneTMP.log 2>&1

# uses more than 4G with 4 threads (4G limit on Circle CI)
make -j${PARALLEL_BUILD} VERBOSE=1  > make_AutoTuneTMP.log 2>&1
cd ../..
