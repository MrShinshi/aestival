#!/usr/bin/env bash
# ─── aestival deploy script ──────────────────────────────────────────────────
# Run from the repository root on the target Linux server.
# Safe for repeated runs: never overwrites existing config or database files.
#
# Usage:
#   ./deploy.sh              # build + deploy binary/config/workspace
#   ./deploy.sh --restart    # also restart the systemd service after deploy
#   ./deploy.sh --no-build   # skip build, only copy files (re-deploy from cache)
#   ./deploy.sh --help       # show this help
# ──────────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"
BIN_DIR="$REPO_ROOT/bin"
BUILD_DIR="$REPO_ROOT/build"
RESTART_SVC=false
NO_BUILD=false

# ── parse flags ──────────────────────────────────────────────────────────────

for arg in "$@"; do
  case "$arg" in
    --restart)  RESTART_SVC=true ;;
    --no-build) NO_BUILD=true ;;
    --help)     head -10 "$0"; exit 0 ;;
    *)          echo "unknown flag: $arg"; exit 1 ;;
  esac
done

echo ":: deploy started at $(date '+%F %T')"

# ── 1. build ─────────────────────────────────────────────────────────────────

if $NO_BUILD; then
  echo ":: [skip] build (--no-build)"
else
  echo ":: [1/4] configuring cmake..."
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

  echo ":: [2/4] building..."
  cmake --build "$BUILD_DIR" --config Release --parallel "$(nproc)"
fi

# ── 2. ensure target directories ─────────────────────────────────────────────

mkdir -p "$BIN_DIR"/{config,contexts,workspace}

# ── 3. deploy binary ─────────────────────────────────────────────────────────

echo ":: [3/4] deploying binary..."
cp "$BUILD_DIR/aestival" "$BIN_DIR/aestival"

# ── 4. deploy config (never overwrite) ────────────────────────────────────────

echo ":: [4/4] deploying config & data..."

# Config — never overwrite.  If bin/config/bot_config.json already exists,
# keep it; otherwise seed from the repo template.
if [ -f "$BIN_DIR/config/bot_config.json" ]; then
  echo "   [keep] config/bot_config.json (already exists)"
else
  cp "$REPO_ROOT/config/bot_config.json" "$BIN_DIR/config/bot_config.json"
  echo "   [seed] config/bot_config.json (fresh from template — edit it!)"
fi

# Workspace — always update.  These are persona/prompt files that evolve with
# the codebase and don't contain secrets.
echo "   [sync] workspace/"
rsync -a --delete "$REPO_ROOT/workspace/" "$BIN_DIR/workspace/"

# Contexts (SQLite DB) — never touch.  This directory holds persistent
# conversation history that must survive deploys.
echo "   [keep] contexts/ (untouched)"

echo ":: deploy finished at $(date '+%F %T')"

# ── 5. optional restart ──────────────────────────────────────────────────────

if $RESTART_SVC; then
  echo ":: restarting aestival-bot.service..."
  sudo systemctl restart aestival-bot.service
  echo ":: service restarted."
  systemctl --no-pager status aestival-bot.service
else
  echo ""
  echo "  To restart the bot:  sudo systemctl restart aestival-bot.service"
  echo "  (or run:  ./deploy.sh --restart)"
fi
