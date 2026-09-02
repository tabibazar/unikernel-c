#!/bin/bash
# Probe the pinned-message-as-shared-register design before building on it.
#
# Why this shape at all: a bot cannot see messages from other bots ("regardless
# of mode", per the Bot FAQ), and getUpdates is single-consumer per token -- two
# pollers on one token get 409 Conflict and steal each other's updates. So
# neither obvious Telegram bus works. One SHARED token plus sendMessage +
# pinChatMessage to write and getChat -> pinned_message to read does: every
# worker is literally the same bot, and getChat has no offset and no long poll.
#
# This checks that the design actually holds against the live API, and measures
# the two numbers R3 needs: the largest payload that survives, and where the
# per-chat rate limit bites.
#
#   TELEGRAM_CHAT_ID=... bash tools/tg_register_probe.sh
#
# WARNING: this posts real messages to a real chat. Point it at a scratch group.
set -eu
TOKEN=$(cat "$HOME/.tg_token")          # never echoed
: "${TELEGRAM_CHAT_ID:?set TELEGRAM_CHAT_ID}"
API="https://api.telegram.org/bot${TOKEN}"

say() { printf '\n=== %s ===\n' "$1"; }

say "0. identity and chat type"
curl -fsS "$API/getMe" | tr ',' '\n' | grep -E '"username"|"id"'
curl -fsS "$API/getChat" -d chat_id="$TELEGRAM_CHAT_ID" | tr ',' '\n' | grep -E '"type"|"title"' || true

say "1. can this bot pin at all"
PAYLOAD="ACO-REGISTER v1 probe $(date -u +%FT%TZ)"
MID=$(curl -fsS "$API/sendMessage" -d chat_id="$TELEGRAM_CHAT_ID" \
        --data-urlencode text="$PAYLOAD" | sed -n 's/.*"message_id":\([0-9]*\).*/\1/p')
echo "message_id=$MID"
# Not -f: the error body IS the finding if pinning is refused.
curl -sS "$API/pinChatMessage" -d chat_id="$TELEGRAM_CHAT_ID" \
     -d message_id="$MID" -d disable_notification=true

say "2. does getChat return it (this is the read path)"
curl -fsS "$API/getChat" -d chat_id="$TELEGRAM_CHAT_ID" \
  | tr ',' '\n' | grep -iA2 'pinned' | head -20

say "3. largest payload that survives"
for n in 1024 2048 3072 4000 4200; do
  BIG=$(head -c "$n" /dev/zero | tr '\0' 'A')
  code=$(curl -s -o /dev/null -w '%{http_code}' "$API/sendMessage" \
         -d chat_id="$TELEGRAM_CHAT_ID" --data-urlencode text="$BIG")
  echo "$n chars -> HTTP $code"
  sleep 1
done

say "4. per-chat rate limit"
for i in $(seq 1 25); do
  code=$(curl -s -o /dev/null -w '%{http_code}' "$API/sendMessage" \
         -d chat_id="$TELEGRAM_CHAT_ID" --data-urlencode text="rate probe $i")
  printf '%s:%s ' "$i" "$code"
  if [ "$code" = "429" ]; then echo; echo "throttled at message $i"; break; fi
done
echo
echo "Record the three findings in tools/README.md: the pin result, the largest"
echo "payload accepted, and the index at which 429 appeared. They set R3's"
echo "migration interval and tour encoding budget."
