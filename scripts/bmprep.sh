#!/bin/bash
# Prepare a clean BareMetal-App tree and launch its build detached.
# Lives in a file so that this script's own command line never contains the
# string we pattern-match on — matching it killed two exec sessions already.
set -u

for p in $(pgrep -f "[s]etup[.]sh"); do sudo kill -9 "$p" 2>/dev/null || true; done
sleep 1

cd "$HOME"
rm -rf BareMetal-App
git clone -q https://github.com/ReturnInfinity/BareMetal-App.git
cp prime_hunter.c BareMetal-App/
cd BareMetal-App

nohup setsid ./setup.sh > /tmp/bm-setup.log 2>&1 < /dev/null &
sleep 5

echo "build pids: $(pgrep -cf '[s]etup[.]sh')"
tail -3 /tmp/bm-setup.log
