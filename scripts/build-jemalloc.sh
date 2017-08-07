#!/bin/bash -e
set -x


if [ ! -d jemalloc ] ; then
    git clone https://github.com/jemalloc/jemalloc.git
    cd jemalloc
    git checkout 4.5.0
    cd ..
fi

cd jemalloc
export CC=${mycc}
export CXX=${mycxx}
export CFLAGS=${mycflags}
export CXXFLAGS=${mycxxflags}
# make clean

autoconf
if [ ! -d build ] ; then
    mkdir build
fi
cd build
../configure CC=cc CXX=CC --prefix=${JEMALLOC_ROOT}
make -j16
make install
cd ../..