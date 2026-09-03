#!/bin/bash
set -e

# Build libgmp.a for the BareMetal target: x86-64, freestanding, musl
# headers, no PIC, large code model.
#
# The two-stage shape here is deliberate. GMP is autotools, and its
# configure runs link tests; handing those -nostdlib makes every test
# fail and configure then concludes the compiler is broken. So configure
# runs HOSTED -- it only needs to pick the CPU's assembly variant and
# generate config.h/gmp.h, neither of which depends on which libc we
# eventually link -- and the actual compilation is redone underneath it
# with the port's own flags, including musl's headers in place of the
# host's. Same trick BareMetal-AppPort already uses for CPython.
#
# GMP_ASM=0 builds --disable-assembly (portable C only). That config is
# too slow to be useful in production, but it isolates toolchain
# problems from assembly/addressing problems, so it is the first pass
# when anything breaks.
#
# GMP_CPU selects the assembly variant. GMP's own configure would detect
# the BUILD machine, which is wrong when this runs in an emulated
# container or on a different host from the target. Set it explicitly.

GMP_VERSION="${GMP_VERSION:-6.3.0}"
GMP_ASM="${GMP_ASM:-1}"
GMP_CPU="${GMP_CPU:-x86_64}"
GMP_FAT="${GMP_FAT:-1}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$DIST_DIR/build}"
GMP_DIR="$BUILD_DIR/gmp-${GMP_VERSION}"

MUSL_INC="${MUSL_INC:?set MUSL_INC to the musl sysroot include dir}"

if [ ! -d "$GMP_DIR" ]; then
	echo "error: $GMP_DIR missing -- run get-gmp.sh first." >&2
	exit 1
fi

# Codegen flags matching BareMetal-AppPort's build-app.sh CFLAGS.
# -nostdlib/-nostdinc are deliberately NOT here: configure needs to be
# able to link and to find headers. They are added for the make pass.
#
# -U_FORTIFY_SOURCE matters even though we never ask for it: Debian's
# gcc enables it by default, and it rewrites plain memcpy/sprintf calls
# into __memcpy_chk/__sprintf_chk, which are glibc symbols musl does not
# define. The failure surfaces only at final link, as undefined symbols
# with no obvious connection to a hardening flag nobody set.
CODEGEN="-O2 -m64 -mcmodel=large -mno-red-zone -fno-pic -fno-pie \
 -fno-stack-protector -fomit-frame-pointer -falign-functions=16 \
 -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
 -U_FORTIFY_SOURCE"

CONFIGURE_ARGS=(
	--disable-shared
	--enable-static
	--host=x86_64-pc-linux-gnu
	--build=x86_64-pc-linux-gnu
	# No C++ bindings: they would drag in libstdc++, which this target
	# has no port of.
	--enable-cxx=no

	# GMP's TMP_ALLOC defaults to alloca, and can take tens of KiB per
	# frame at these operand sizes. On this target the stack is supplied
	# by the BareMetal kernel at a low address, its size is not
	# discoverable from the port, and there is no guard page and no
	# paging -- so an overflow does not fault, it silently corrupts
	# whatever sits below, plausibly the kernel's own call vectors at
	# 0x00100010. Routing temporaries to malloc instead puts them on the
	# bump heap, which is sized from b_system(FREE_MEMORY) and reports
	# exhaustion cleanly.
	--enable-alloca=malloc-reentrant

	# configure runs against the host's glibc and so detects
	# obstack_vprintf, which is glibc-specific; the make pass then fails
	# on a missing <obstack.h> because musl has no obstack. Nothing here
	# wants GMP's obstack printf variants, so the check is answered
	# directly rather than letting a host fact leak into a target build.
	# Any other glibc-only function GMP probes would need the same
	# treatment -- this is the one that actually bit.
	ac_cv_func_obstack_vprintf=no
)

if [ "$GMP_ASM" = "0" ]; then
	CONFIGURE_ARGS+=(--disable-assembly)
elif [ "$GMP_FAT" = "1" ]; then
	# Runtime cpuid dispatch across every x86-64 variant. Costs image
	# size, but it is the only correct choice when the build host is not
	# the run host -- which is always true here, since this builds under
	# emulation and runs on a cloud metal instance.
	CONFIGURE_ARGS+=(--enable-fat)
fi

cd "$GMP_DIR"

echo "=== configure (hosted, port codegen; asm=$GMP_ASM fat=$GMP_FAT) ==="
./configure "${CONFIGURE_ARGS[@]}" CFLAGS="$CODEGEN" > configure.log 2>&1 || {
	echo "configure failed; tail of configure.log:" >&2
	tail -40 configure.log >&2
	exit 1
}

grep -E '^#define (HAVE_HOST_CPU|WANT_FAT)' config.h || true

echo "=== make (musl headers substituted) ==="
make -j"$(nproc)" CFLAGS="$CODEGEN -ffreestanding -nostdinc -isystem $MUSL_INC" > make.log 2>&1 || {
	echo "make failed; tail of make.log:" >&2
	tail -60 make.log >&2
	exit 1
}

LIB=".libs/libgmp.a"
[ -f "$LIB" ] || LIB="libgmp.a"
echo "=== built $LIB ==="
ls -l "$LIB"
size "$LIB" 2>/dev/null | tail -1 || true

echo "=== undefined symbols in libgmp.a (the libc surface it needs) ==="
nm -u --no-sort "$LIB" 2>/dev/null | grep -v '^$' | sed 's/^ *U *//' | sort -u
