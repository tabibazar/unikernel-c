#!/bin/sh
# Run the Flaneur agent without ever putting a secret on a command line or in
# shell history. Keys live in ~/.gemini_key and ~/.tg_token, mode 600.
#
#   ./wander.sh            one wander, then exit
#   ./wander.sh --loop     the 24/7 daemon
set -eu

BIN="${BIN:-./agent}"          # BIN=./agent_static for the BareMetal build

[ -r "$HOME/.gemini_key" ] || { echo "missing ~/.gemini_key" >&2; exit 1; }
[ -r "$HOME/.tg_token" ]   || { echo "missing ~/.tg_token" >&2; exit 1; }

GEMINI_API_KEY=$(cat "$HOME/.gemini_key")
TELEGRAM_BOT_TOKEN=$(cat "$HOME/.tg_token")
TELEGRAM_CHAT_ID="${TELEGRAM_CHAT_ID:-${TELEGRAM_CHAT_ID:?set TELEGRAM_CHAT_ID}}"
export GEMINI_API_KEY TELEGRAM_BOT_TOKEN TELEGRAM_CHAT_ID

# LLM_MODEL overrides the built-in default without a recompile.
[ "${1:-}" = "--loop" ] && exec "$BIN" || exec "$BIN" --once
