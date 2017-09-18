#!/bin/bash -e
set -x
set -e

if [[ ! $PARALLEL_BUILD ]]; then
    echo "MatrixMultiplicationHPX: PARALLEL_BUILD not set, defaulting to 1"
    export PARALLEL_BUILD=1
fi

# git pull

mkdir -p build_Release
cd build_Release

# detection of Vc doesn't work with a relative path
# cmake -DVc_ROOT="$MatrixMultiplicationHPX_ROOT" -DCMAKE_BUILD_TYPE=release ../
cmake -DHPX_ROOT="$HPX_ROOT" -DAutoTuneTMP_ROOT=${AutoTuneTMP_ROOT} -DCMAKE_BUILD_TYPE=Release ../

# uses more than 4G with 4 threads (4G limit on Circle CI)
make -j${PARALLEL_BUILD} VERBOSE=1
cd ../

mkdir -p build_RelWithDebInfo
cd build_RelWithDebInfo

# detection of Vc doesn't work with a relative path
# cmake -DVc_ROOT="$MatrixMultiplicationHPX_ROOT" -DCMAKE_BUILD_TYPE=release ../
cmake -DHPX_ROOT="$HPX_ROOT" -DAutoTuneTMP_ROOT=${AutoTuneTMP_ROOT} -DCMAKE_BUILD_TYPE=RelWithDebInfo ../

# uses more than 4G with 4 threads (4G limit on Circle CI)
make -j${PARALLEL_BUILD} VERBOSE=1
cd ../
