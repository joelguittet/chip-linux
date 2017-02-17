#!/bin/bash

set -ex

INPUTDIR=${INPUTDIR:-deb}
BASEDIR=${BASEDIR:-repo}
CODENAME=${CODENAME:-stable}
COMPONENTS=${COMPONENTS:-main}
ARCHITECTURE=${ARCHITECTURE:-armhf}

echo "${GPG_SECRET}" | gpg --import
PUBKEY=$(gpg --list-keys --with-colons "Next Thing Co." | awk -F: '/^pub:/ { print $5 }')

mkdir -p ${BASEDIR}/conf
echo -e "\
Codename: ${CODENAME} \n\
Components: ${COMPONENTS} \n\
Architectures: ${ARCHITECTURE} \n\
SignWith: ${PUBKEY} \n\
" > ${BASEDIR}/conf/distributions

# Export pubkey
gpg --armor --output ${BASEDIR}/public.key --export "${PUBKEY}"

reprepro -b "${BASEDIR}" includedeb ${CODENAME} ${INPUTDIR}/*.deb

