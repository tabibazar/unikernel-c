#!/bin/bash
# build-worker-docker.sh -- build cc_worker into a BareMetal unikernel image
# without renting a machine.
#
# WHY DOCKER AND NOT A RENTED HOST
#
# Building a unikernel image needs Linux x86-64 and a toolchain; it does NOT
# need KVM. Only *running* Firecracker does. That distinction is worth stating
# because it was missed once already here, and it is the difference between a
# free build and a rented bare-metal instance: an amd64 container under
# emulation on an Apple-silicon Mac can produce the image perfectly well, just
# slowly.
#
# Slowly is acceptable. The build runs once per slice set and the results are
# cached in a named volume, so the emulation cost is paid once rather than per
# worker.
#
#   ./build-worker-docker.sh <slice-id> <m-start> <m-count>
#
# Produces  results/workers/<slice-id>/baremetal.elf  ready for bm-api.sh.
set -euo pipefail

SLICE_ID="${1:?usage: build-worker-docker.sh <slice-id> <m-start> <m-count>}"
M_START="${2:?}"
M_COUNT="${3:?}"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"      # .../gmp
ROOT="$(cd "$REPO/.." && pwd)"                                # repo root
OUTDIR="$REPO/results/workers/$SLICE_ID"
VOLUME="${VOLUME:-bmport-build}"
IMAGE="${IMAGE:-bmworker-build:latest}"

mkdir -p "$OUTDIR"

# --- 1. the slice header, generated on the host where GMP already works ------
"$REPO/scripts/mkslice.sh" "$SLICE_ID" "$M_START" "$M_COUNT" "$OUTDIR/cc_slice.h"

# --- 2. a builder image, once ------------------------------------------------
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
	echo "=== building the builder image (amd64) ==="
	docker build --platform linux/amd64 -t "$IMAGE" -f - "$ROOT" <<'DOCKERFILE'
FROM --platform=linux/amd64 ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
# setup.sh's own preflight requires exactly:
#   git curl unzip tar gcc nasm make patch jq mkfs.ext2
# so jq and e2fsprogs are not optional extras -- the script exits without them.
# The rest is the AppPort dependency stack (musl, lwIP, mbedTLS, curl, SQLite,
# libsodium, lwext4, CPython).
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential nasm git curl ca-certificates unzip tar xz-utils \
      patch jq e2fsprogs \
      python3 python3-dev pkg-config file bc m4 autoconf automake libtool \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
DOCKERFILE
fi

# --- 3. clone + full port build, cached in a volume --------------------------
# setup.sh is unconditional about CPython, which is the slowest part under
# emulation. It is left alone rather than patched: a locally-modified build of
# the port is not the thing we want to prove works.
docker run --rm --platform linux/amd64 \
	-v "$VOLUME:/work" -v "$ROOT:/repo:ro" -v "$OUTDIR:/out" \
	"$IMAGE" bash -eu -c '
	if [ ! -d /work/BareMetal-App ]; then
		echo "=== cloning BareMetal-App ==="
		git clone --recursive https://github.com/ReturnInfinity/BareMetal-App.git /work/BareMetal-App
	fi
	cd /work/BareMetal-App
	git pull --ff-only || true
	( cd BareMetal-AppPort && git pull --ff-only || true )

	if [ ! -f BareMetal-AppPort/build/musl-1.2.6/lib/libc.a ]; then
		echo "=== setup.sh (slow under emulation; runs once) ==="
		./setup.sh
	else
		echo "=== port already built, reusing ==="
	fi

	export APPPORT=/work/BareMetal-App/BareMetal-AppPort
	export BMAPP=/work/BareMetal-App

	# /repo is mounted read-only, and both GMP scripts default BUILD_DIR to a
	# path inside it, so it has to be pointed at the writable volume. MUSL_INC
	# is the sysroot the port installs musl headers into -- the same path
	# link-app.sh derives for itself.
	export BUILD_DIR=/work/gmp-build
	export MUSL_INC=$APPPORT/build/musl-1.2.6/sysroot/usr/local/musl/include
	[ -d "$MUSL_INC" ] || { echo "musl sysroot missing at $MUSL_INC" >&2; exit 1; }

	GMP_SRC=$BUILD_DIR/gmp-6.3.0
	if [ ! -f "$GMP_SRC/.libs/libgmp.a" ] && [ ! -f "$GMP_SRC/libgmp.a" ]; then
		echo "=== fetching and building GMP for the target ==="
		bash /repo/gmp/scripts/get-gmp.sh
		bash /repo/gmp/scripts/build-gmp.sh
	else
		echo "=== target libgmp.a already built, reusing ==="
	fi

	echo "=== linking cc_worker for slice ==="
	mkdir -p /work/applink
	cp /out/cc_slice.h /work/applink/cc_slice.h
	OUT_DIR=/work/applink \
	GMP_BUILD="$GMP_SRC" \
	CPPFLAGS="-I/work/applink" \
		bash /repo/gmp/scripts/link-app.sh /repo/gmp/cc_worker.c

	echo "=== wrapping into baremetal.elf ==="
	cp /work/applink/cc_worker.app BareMetal-Firecracker/sys/
	( cd BareMetal-Firecracker && ./build.sh cc_worker.app )
	cp BareMetal-Firecracker/sys/baremetal.elf /out/baremetal.elf
	ls -l /out/baremetal.elf
'

echo
echo "worker image: $OUTDIR/baremetal.elf"
