#!/bin/bash

CPUINFO=`cat /proc/cpuinfo`
DIR="generic64"
CFLAGS=""

if [[ $CPUINFO =~ avx ]]; then
        DIR="AVX"
fi

if [[ $CPUINFO =~ avx2 ]]; then
        DIR="AVX2"
fi

if [[ $CPUINFO =~ avx512 ]]; then
        DIR="AVX512"
fi

echo "Using $DIR XKCP implementation"
mkdir -p build/lib
mkdir -p build/headers
cd XKCP
make $DIR/libXKCP.so
cp bin/$DIR/libXKCP.so ../build/lib
cp bin/$DIR/libXKCP.so.headers/* ../build/headers
