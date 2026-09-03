#!/bin/bash
set -e

# Link a compute-only BareMetal app against musl + GMP.
#
# This is build-app.sh reduced to what an integer workload actually needs.
# The full script links lwIP, mbedTLS, curl, SQLite, libsodium, lwext4 and
# CPython into every app and relies on --gc-sections to drop the unused
# parts. That is the right default for a general tool and the wrong one
# here, for a reason specific to this platform: the heap is a bump
# allocator running from __bss_stop up to __image_base + the VM's RAM, so
# image size is subtracted directly from the memory GMP has to work in.
# On a 16 MiB instance that is not a rounding error.
#
# Usage: link-app.sh <app.c> [more.c ...]

APPPORT="${APPPORT:?set APPPORT to a BareMetal-AppPort checkout}"
GMP_BUILD="${GMP_BUILD:?set GMP_BUILD to the configured gmp source dir}"
OUT_DIR="${OUT_DIR:-$PWD/build}"

PORT="$APPPORT/port"
MUSL_DIR="$APPPORT/build/musl-1.2.6"
MUSL_INC="$MUSL_DIR/sysroot/usr/local/musl/include"
MUSL_LIB="$MUSL_DIR/lib/libc.a"

GMP_INC="$GMP_BUILD"
GMP_LIB="$GMP_BUILD/.libs/libgmp.a"
[ -f "$GMP_LIB" ] || GMP_LIB="$GMP_BUILD/libgmp.a"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GMP_PORT="$(dirname "$SCRIPT_DIR")/port/gmp_port"

for f in "$MUSL_LIB" "$GMP_LIB" "$PORT/c.ld"; do
	[ -f "$f" ] || { echo "error: missing $f" >&2; exit 1; }
done

[ $# -ge 1 ] || { echo "usage: $0 <app.c> [more.c ...]" >&2; exit 1; }
mkdir -p "$OUT_DIR"

# Identical to build-app.sh's CFLAGS. -mcmodel=large is what makes the
# 0xFFFF800000000000 load address expressible; -mno-red-zone matters
# because this platform's interrupt path is known not to preserve state
# correctly, so anything living below rsp is liable to be clobbered.
CFLAGS="-c -m64 -nostdlib -nostartfiles -nodefaultlibs -ffreestanding \
 -fno-pic -fno-pie -mcmodel=large -falign-functions=16 -fomit-frame-pointer \
 -mno-red-zone -fno-builtin -fno-stack-protector -ffunction-sections \
 -fdata-sections -nostdinc -isystem $MUSL_INC"

LIBGCC="$(gcc -m64 -print-libgcc-file-name)"

echo "=== compiling port objects ==="
gcc $CFLAGS -o "$OUT_DIR/crt0.o"          "$PORT/crt0.c"
gcc $CFLAGS -o "$OUT_DIR/posix_shim.o"    "$PORT/posix_shim.c"
gcc $CFLAGS -o "$OUT_DIR/thread_shim.o"   "$PORT/thread_shim.c"
gcc $CFLAGS -o "$OUT_DIR/libBareMetal.o"  "$PORT/libBareMetal.c"
gcc $CFLAGS -I "$PORT" -o "$OUT_DIR/min_stubs.o" "$GMP_PORT/min_stubs.c"

echo "=== compiling app ==="
APP_OBJS=""
APP_NAME="$(basename "$1" .c).app"
for src in "$@"; do
	obj="$OUT_DIR/$(basename "$src" .c).o"
	gcc $CFLAGS -I "$GMP_INC" -o "$obj" "$src"
	APP_OBJS="$APP_OBJS $obj"
done

# Two stages, not a direct link to c.ld's OUTPUT_FORMAT(binary): that
# format silently defeats --gc-sections. build-app.sh documents having
# discovered this the hard way; the ELF intermediate collects correctly
# and objcopy then flattens an already-collected image.
#
# --orphan-handling=warn is added here and is NOT in build-app.sh. c.ld
# places .text/.rodata/.data/.tdata/.tbss/.init_array/.fini_array/.bss and
# nothing else. GMP's assembly emits jump tables into .data.rel.ro.local
# (see mpn/x86_64/x86_64-defs.m4's JUMPTABSECT), which c.ld has no rule
# for. An unplaced allocated section that lands after __bss_stop would be
# aliased straight onto the malloc heap and silently corrupted -- which is
# the exact bug c.ld was already patched for once, for .lrodata/.ldata.
# Warning is cheap; discovering it at runtime on a metal instance is not.
echo "=== linking ==="
ld --gc-sections --orphan-handling=warn --no-warn-rwx-segments \
   --oformat elf64-x86-64 -T "$PORT/c.ld" -o "$OUT_DIR/$APP_NAME.elf" \
   "$OUT_DIR/crt0.o" "$OUT_DIR/posix_shim.o" "$OUT_DIR/thread_shim.o" \
   "$OUT_DIR/libBareMetal.o" "$OUT_DIR/min_stubs.o" $APP_OBJS \
   "$GMP_LIB" "$MUSL_LIB" "$LIBGCC"

objcopy -O binary "$OUT_DIR/$APP_NAME.elf" "$OUT_DIR/$APP_NAME"

echo "=== result ==="
ls -l "$OUT_DIR/$APP_NAME"
echo "--- undefined symbols (must be empty):"
nm -u "$OUT_DIR/$APP_NAME.elf" || true
echo "--- assembly actually linked in (not a C fallback):"
nm "$OUT_DIR/$APP_NAME.elf" | grep -E '__gmpn_(mul_basecase|addmul_1)' | head -5 || true
echo "--- sections:"
objdump -h "$OUT_DIR/$APP_NAME.elf" | awk 'NR>4 && $2 ~ /^\./ {printf "%-24s %8s @ %s\n", $2, $3, $4}'
