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

# A cached tarball is only worth reusing if it is actually the tarball. An
# interrupted download leaves a short file that "already exists", and without
# the check below every later run skips the download, fails the checksum and
# dies -- the same poisoned-cache behaviour this repo reported upstream in
# BareMetal-AppPort's get-*.sh scripts (docs/upstream-reports/). It cost an
# hour here before the irony was noticed, so: verify the cache, and throw it
# away when it is wrong rather than failing forever.
if [ -f "$TARBALL" ]; then
	if echo "${SHA256}  ${TARBALL}" | sha256sum -c - > /dev/null 2>&1; then
		echo "- $TARBALL already exists and matches its checksum - skipping download."
	else
		echo "- $TARBALL exists but is corrupt or truncated - refetching."
		rm -f "$TARBALL"
	fi
fi

if [ ! -f "$TARBALL" ]; then
	echo "- Downloading ${URL}"
	# --fail so an HTTP error is an error rather than an error page saved as
	# the artifact; remove the partial file so a failure cannot poison the
	# next run.
	curl -sSfL -o "${TARBALL}" "${URL}" || { rm -f "${TARBALL}"; echo "download failed" >&2; exit 1; }
fi

echo "- Verifying checksum"
if ! echo "${SHA256}  ${TARBALL}" | sha256sum -c - > /dev/null; then
	rm -f "${TARBALL}"
	echo "checksum mismatch after download -- removed ${TARBALL}" >&2
	exit 1
fi

echo "- Extracting ${TARBALL}"
tar -xf "${TARBALL}"
