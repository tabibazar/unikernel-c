#!/bin/bash
set -e

# Pinned (not "latest"): port/gmp_port/ is written against this exact
# release's configure/config.h layout and its mpn/x86_64 assembly tree.
# Bump deliberately, not automatically.
#
# GMP is vendored unmodified. Every port-side decision lives in
# build-gmp.sh's configure flags instead of a patch, because nothing in
# GMP needs changing for this target: its mpn assembly is pure
# computation -- no syscalls, no libm, no OS surface at all -- and the
# only libc it reaches on the computation path is malloc/free/realloc
# plus the mem* family, all of which musl already provides here.
VERSION="6.3.0"
URL="https://gmplib.org/download/gmp/gmp-${VERSION}.tar.xz"
TARBALL="gmp-${VERSION}.tar.xz"
GMP_DIR="gmp-${VERSION}"

# Upstream's published SHA-256 for gmp-6.3.0.tar.xz. Checked because
# this fetches a compiler-adjacent binary dependency over the network
# into a build that later runs unattended in a swarm.
SHA256="a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$DIST_DIR/build}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ -d "$GMP_DIR" ]; then
	echo "$GMP_DIR already exists - skipping download. Remove it first if you want to re-fetch."
	exit 0
fi

if [ -f "$TARBALL" ]; then
	echo "- $TARBALL already exists - skipping download."
else
	echo "- Downloading ${URL}"
	curl -s -L -o "${TARBALL}" "${URL}"
fi

echo "- Verifying checksum"
echo "${SHA256}  ${TARBALL}" | sha256sum -c - > /dev/null

echo "- Extracting ${TARBALL}"
tar -xf "${TARBALL}"
