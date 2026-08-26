#!/bin/bash
# Wait for the library build to finish, then build the unikernel around
# prime_hunter.c. Runs detached; poll /tmp/bm-stage.log for progress.
set -u
exec > /tmp/bm-stage.log 2>&1

echo "=== waiting for library build, started watching $(date -Is) ==="
while pgrep -f "[s]etup[.]sh" > /dev/null; do sleep 20; done

echo "=== library build finished $(date -Is) ==="
tail -6 /tmp/bm-setup.log

cd "$HOME/BareMetal-App" || exit 1

echo "=== building unikernel $(date -Is) ==="
./1-build.sh prime_hunter.c
echo "BUILD_EXIT=$?"

echo "=== artifacts ==="
ls -l baremetal.elf prime_hunter.app 2>/dev/null
echo "=== done $(date -Is) ==="
