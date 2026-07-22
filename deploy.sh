#!/usr/bin/env bash
# ─── aestival deploy script ──────────────────────────────────────────────────
# Downloads the latest CI artifact from GitHub Actions and deploys it to the
# target Linux server via scp/ssh.
#
# Prerequisites (local machine):
#   gh CLI (>= 2.x, authenticated with repo + workflow scopes)
#   ssh access to TARGET
#
# Usage:
#   ./deploy.sh                    # deploy from latest CI run on current branch
#   ./deploy.sh --branch main      # deploy from a different branch's CI
#   ./deploy.sh --restart          # also restart the systemd service
#   ./deploy.sh --help             # show this help
#
# Environment variables (optional):
#   AESTIVAL_TARGET   default: shinshi@122.51.129.97
#   AESTIVAL_REMOTE_DIR default: /home/shinshi/aestival
# ──────────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"
TARGET="${AESTIVAL_TARGET:-shinshi@122.51.129.97}"
REMOTE_DIR="${AESTIVAL_REMOTE_DIR:-/home/shinshi/aestival}"
REMOTE_BIN="$REMOTE_DIR/bin"
TEMP_DIR="$(mktemp -d)"
RESTART_SVC=false
BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo shinshi)"
WORKFLOW="ci.yml"
ARTIFACT_NAME="aestival-linux-gcc"

# ── parse flags ──────────────────────────────────────────────────────────────

while [ $# -gt 0 ]; do
  case "$1" in
    --restart)  RESTART_SVC=true; shift ;;
    --branch)   BRANCH="$2"; shift 2 ;;
    --help)     sed -n '2,16p' "$0"; exit 0 ;;
    *)          echo "unknown flag: $1"; exit 1 ;;
  esac
done

cleanup() { rm -rf "$TEMP_DIR"; }
trap cleanup EXIT

echo ":: deploy started at $(date '+%F %T')"
echo "   branch=$BRANCH  target=$TARGET"

# ── 1. download CI artifact ──────────────────────────────────────────────────

echo ":: [1/4] finding latest CI run on branch '$BRANCH'..."

RUN_ID=$(gh run list \
  --branch "$BRANCH" \
  --workflow "$WORKFLOW" \
  --status success \
  --limit 1 \
  --json databaseId \
  --jq '.[0].databaseId' \
  2>&1) || { echo "ERROR: gh run list failed. Is gh authenticated?"; echo "$RUN_ID"; exit 1; }

if [ -z "$RUN_ID" ] || [ "$RUN_ID" = "null" ]; then
  echo "ERROR: no successful CI run found for branch '$BRANCH'"
  echo "  Push to trigger CI, or use --branch to specify another branch."
  exit 1
fi

RUN_SHA=$(gh run list \
  --branch "$BRANCH" \
  --workflow "$WORKFLOW" \
  --status success \
  --limit 1 \
  --json headSha \
  --jq '.[0].headSha' 2>/dev/null)
echo "   run=$RUN_ID  sha=${RUN_SHA:0:8}"

echo ":: [2/4] downloading artifact '$ARTIFACT_NAME'..."
gh run download "$RUN_ID" --name "$ARTIFACT_NAME" --dir "$TEMP_DIR" 2>&1

BINARY="$TEMP_DIR/aestival"
if [ ! -f "$BINARY" ]; then
  echo "ERROR: downloaded artifact does not contain 'aestival' binary"
  ls -la "$TEMP_DIR"
  exit 1
fi

chmod +x "$BINARY"
echo "   binary size: $(du -h "$BINARY" | cut -f1)"

# ── 2. deploy binary (atomic: scp as .new, then mv on server) ────────────────

echo ":: [3/4] deploying binary..."
scp "$BINARY" "$TARGET:$REMOTE_BIN/aestival.new"

# ── 3. deploy config & workspace ─────────────────────────────────────────────

echo ":: [4/4] deploying config & workspace..."

# Config — seed from template only if none exists on the server.
# Never overwrite an existing config (contains secrets).
ssh "$TARGET" "
  if [ ! -f '$REMOTE_BIN/config/bot_config.json' ]; then
    echo '   [seed] config/bot_config.json (first deploy — edit it!)'
  else
    echo '   [keep] config/bot_config.json (already exists)'
  fi
"

# Workspace — always sync from the local repo (these evolve with the codebase).
echo "   [sync] workspace/"
scp -r "$REPO_ROOT/workspace/"* "$TARGET:$REMOTE_BIN/workspace/"

# Contexts — never touch.
echo "   [keep] contexts/ (untouched)"

# ── 4. finalise (atomic binary swap + optional restart) ──────────────────────

ssh "$TARGET" "
  set -e
  mv '$REMOTE_BIN/aestival.new' '$REMOTE_BIN/aestival'
  echo ':: binary atomically replaced'
"

echo ":: deploy finished at $(date '+%F %T')"

if $RESTART_SVC; then
  echo ":: restarting aestival-bot.service..."
  ssh -t "$TARGET" "sudo systemctl restart aestival-bot.service" 2>&1
  echo ""
  echo ":: service status:"
  ssh "$TARGET" "systemctl --no-pager status aestival-bot.service" 2>&1 || true
else
  echo ""
  echo "  To restart:  ./deploy.sh --restart"
fi
