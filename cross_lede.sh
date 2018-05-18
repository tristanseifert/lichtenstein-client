#!/bin/sh

# Set up paths and environment for cross compiling
export STAGING_DIR=/home/tristan/onion_lede/staging_dir
export TOOLCHAIN_DIR=$STAGING_DIR/toolchain-mipsel_24kc_gcc-5.4.0_musl-1.1.16
export LDCFLAGS=$TOOLCHAIN_DIR/usr/lib
export LD_LIBRARY_PATH=$TOOLCHAIN_DIR/usr/lib
export PATH=$TOOLCHAIN_DIR/bin:$PATH

# tools
export CC=$TOOLCHAIN_DIR/bin/mipsel-openwrt-linux-gcc
export CXX=$TOOLCHAIN_DIR/bin/mipsel-openwrt-linux-g++
export AS=$TOOLCHAIN_DIR/bin/mipsel-openwrt-linux-as

# include/library search paths
export INC_DIRS="$STAGING_DIR/target-mipsel_24kc_musl-1.1.16/usr/include"
export LIBS_DIRS="$STAGING_DIR/target-mipsel_24kc_musl-1.1.16/usr/lib"
export LDFLAGS="-Wl,--allow-shlib-undefined"

make
