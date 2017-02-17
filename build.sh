#!/bin/bash

set -ex

export CI_BUILD_ID=${CI_BUILD_REF_NAME:-$(git rev-parse --abbrev-ref HEAD)}
export CI_BUILD_ID=${CI_BUILD_ID:-667}
export YOUR_FULLNAME="Next Thing Co."
export YOUR_EMAIL="software@nextthing.co"
export N=$(( $(nproc) * 2 ))

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export KBUILD_DEBARCH=armhf
export KDEB_CHANGELOG_DIST=jessie
export LOCALVERSION=-${CI_BUILD_REF_NAME}
#export KDEB_SOURCENAME="linux-image-$(make kernelversion)"
export KDEB_PKGVERSION=$(make kernelversion)-${CI_BUILD_ID}
export DEBFULLNAME="${YOUR_FULLNAME}"
export DEBEMAIL="${YOUR_EMAIL}"

git config user.email "${YOUR_EMAIL}"
git config user.name "${YOUR_FULLNAME}"

make multi_v7_defconfig
make -j${N} prepare modules_prepare scripts
make -j${N} deb-pkg
mv ../*.deb ../*.gz ../*.dsc  ../*.changes artifacts/deb/
make -j${N} INSTALL_PATH=/build/artifacts/boot install
make -j${N} INSTALL_MOD_PATH=/build/artifacts modules_install
make -j${N} INSTALL_HDR_PATH=/build/artifacts/usr headers_install
make -j${N} INSTALL_FW_PATH=/build/artifacts/lib/firmware firmware_install
cp arch/arm/boot/dts/*.dtb artifacts/boot/dtbs/

./build_wifi.sh rtl8723ds
./build_wifi.sh rtl8723bs

# create deb repo
INPUTDIR=/build/artifacts/deb ./create_deb_repository.sh
pushd repo
echo "<html><head><title>Build $CI_BUILD_ID</title></head><body>" >index.html
find . -name "*.deb" -exec echo "<a href=\""\{\}"\">"\{\}"</a><br/>" \; >>index.html
echo "</body></html>" >>index.html
surge --project $PWD --domain ${CI_BUILD_REF_NAME//./_}.surge.sh
popd
