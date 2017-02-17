#!/bin/bash

set -x

export FLAVOR=${FLAVOR:-$1}
export FLAVOR=${FLAVOR:-rtl8723bs}

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export LINUX_SRCDIR="$(pwd)"
export RTL8723_SRCDIR="$(pwd)/$FLAVOR"
export BUILDDIR=$RTL8723_SRCDIR/build
export CONCURRENCY_LEVEL=$(( $(nproc) * 2 ))
export REPO=https://github.com/NextThingCo/$FLAVOR

git clone $REPO $RTL8723_SRCDIR
pushd $RTL8723_SRCDIR
git checkout debian

export RTL_VER=$(cd $RTL8723_SRCDIR; dpkg-parsechangelog --show-field Version)

dpkg-buildpackage -A -uc -us -nc

sudo dpkg -i ../${FLAVOR}-mp-driver-source_${RTL_VER}_all.deb

mkdir -p $BUILDDIR/usr_src
export CC=arm-linux-gnueabihf-gcc
export $(dpkg-architecture -aarmhf)
export CROSS_COMPILE=arm-linux-gnueabihf-
export KERNEL_VER=$(cd $LINUX_SRCDIR; make kernelversion)

cp -a /usr/src/modules/${FLAVOR}-mp-driver/* $BUILDDIR

m-a -t -u $BUILDDIR \
    -l $KERNEL_VER \
    -k $LINUX_SRCDIR \
    build ${FLAVOR}-mp-driver-source

mv ../*.deb build/*.deb ../*.changes ../artifacts/deb/
popd
