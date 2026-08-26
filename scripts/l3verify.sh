#!/bin/bash
# Rebuild with an explicit send-acknowledgement line and a fast heartbeat,
# then run it in L2 so the serial console proves the posts are landing.
set -u
cd "$HOME" || exit 1

TG=$(cat .tg_token)
cp prime_hunter_new.c prime_hunter.c
sed -i "s|PUT_BOT_TOKEN_HERE|${TG}|; s|PUT_CHAT_ID_HERE|${TELEGRAM_CHAT_ID:?set TELEGRAM_CHAT_ID}|" prime_hunter.c
# Fast heartbeat for this run only; the shipped default stays 1800.
sed -i "s|#define HEARTBEAT_SECONDS  1800|#define HEARTBEAT_SECONDS  45|" prime_hunter.c
grep -m1 "define HEARTBEAT_SECONDS" prime_hunter.c

cp prime_hunter.c BareMetal-App/prime_hunter.c
cd BareMetal-App || exit 1
./1-build.sh prime_hunter.c 2>&1 | tail -3

SSH="sshpass -p bm ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@127.0.0.1"
SCP="sshpass -p bm scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2222"

$SCP baremetal.elf ubuntu@127.0.0.1:~/bm/ 2>/dev/null
echo "=== running in L2 for 180s ==="
$SSH 'cd ~/bm && sudo ./baremetal.sh start >/dev/null 2>&1; echo started' 2>/dev/null
sleep 180
$SSH 'cd ~/bm && sudo ./baremetal.sh output --full 2>&1 | head -60' 2>/dev/null
$SSH 'cd ~/bm && sudo ./baremetal.sh stop >/dev/null 2>&1; echo "(stopped)"' 2>/dev/null
