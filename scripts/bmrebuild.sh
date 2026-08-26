#!/bin/bash
# Re-bake credentials into the fresh source and rebuild the unikernel.
set -u
cd "$HOME" || exit 1

TG=$(cat .tg_token)
cp prime_hunter_new.c prime_hunter.c
sed -i "s|PUT_BOT_TOKEN_HERE|${TG}|; s|PUT_CHAT_ID_HERE|${TELEGRAM_CHAT_ID:?set TELEGRAM_CHAT_ID}|" prime_hunter.c
echo "placeholders left: $(grep -c 'PUT_BOT_TOKEN_HERE\|PUT_CHAT_ID_HERE' prime_hunter.c || true)"

cp prime_hunter.c BareMetal-App/prime_hunter.c
cd BareMetal-App || exit 1
./1-build.sh prime_hunter.c 2>&1 | tail -4
echo "BUILD_EXIT=$?"
ls -l baremetal.elf
