#!/bin/bash
# Deploy the built unikernel to BareMetal Cloud.
#
# Everything is already built and proven; this is the only step that needs a
# credential I could not create for you. Get a key from
# https://baremetal.returninfinity.com/dashboard (it looks like bmvps_...), then:
#
#   BM_API_KEY=bmvps_... bash cloudup.sh
#
# RAM: BareMetal Cloud caps instances at 16 MiB, and that is genuinely enough —
# the deployed instance completes TLS handshakes and posts to Telegram at 16 MiB.
# (I had guessed mbedTLS would need far more and used 256 MiB in local testing;
# the cloud rejected that with "ramMib exceeds the maximum of 16" and then ran
# fine anyway.)
set -eu

: "${BM_API_KEY:?set BM_API_KEY first — see the header of this script}"

cd "$HOME/BareMetal-App" || exit 1
[ -f baremetal.elf ] || { echo "baremetal.elf missing — run ./1-build.sh prime_hunter.c" >&2; exit 1; }

NAME=prime-hunter
VCPU=1
RAM_MIB=16

echo "=== uploading image ==="
IMAGE_ID=$(./bm-api.sh images upload "$NAME" baremetal.elf | awk -F': ' '/^id:/{print $2}')
echo "image id: $IMAGE_ID"

echo "=== creating instance (${VCPU} vcpu, ${RAM_MIB} MiB) ==="
INSTANCE_ID=$(./bm-api.sh instances create "$NAME" "$VCPU" "$RAM_MIB" "$IMAGE_ID" | awk -F': ' '/^id:/{print $2}')
echo "instance id: $INSTANCE_ID"

echo
echo "Watch it at https://baremetal.returninfinity.com/dashboard"
echo "Telegram should start receiving within a few seconds of boot."
