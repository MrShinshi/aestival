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
#   ./deploy.sh --sync             # after PR merge: reset current branch to main
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

# Server host key for TOFU protection (Ed25519).
SERVER_HOST_KEY="${AESTIVAL_HOST_KEY:-ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIL05yYQVDpmc3oO21VpAvdaMBPOQeUvpOeihVCar6Msn}"
KNOWN_HOSTS_FILE="$(mktemp)"
echo "122.51.129.97 $SERVER_HOST_KEY" > "$KNOWN_HOSTS_FILE"
SSH_CMD="ssh -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$KNOWN_HOSTS_FILE"
RESTART_SVC=false
SYNC_BRANCH=false
BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo shinshi)"
WORKFLOW="ci.yml"
ARTIFACT_NAME="aestival-linux-gcc"

# ── parse flags ──────────────────────────────────────────────────────────────

while [ $# -gt 0 ]; do
  case "$1" in
    --restart)  RESTART_SVC=true; shift ;;
    --sync)     SYNC_BRANCH=true; shift ;;
    --branch)   BRANCH="$2"; shift 2 ;;
    --help)     sed -n '2,17p' "$0"; exit 0 ;;
    *)          echo "unknown flag: $1"; exit 1 ;;
  esac
done

# ── sync mode: reset current branch to main (post-PR cleanup) ───────────────

if $SYNC_BRANCH; then
  CURRENT="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)"
  # Safety: refuse to force-push protected branches.
  case "$CURRENT" in
    main|master)
      echo "ERROR: refusing to force-push protected branch '$CURRENT'" >&2
      exit 1
      ;;
  esac
  echo ":: syncing '$CURRENT' -> origin/main..."
  git -C "$REPO_ROOT" fetch origin main
  git -C "$REPO_ROOT" reset --hard origin/main
  git -C "$REPO_ROOT" push --force origin "$CURRENT"
  echo ":: done — '$CURRENT' force-pushed to main HEAD ($(git -C "$REPO_ROOT" rev-parse --short HEAD))"
  echo ":: tip: re-apply any local-only config (e.g. bot_config.json tokens)"
  exit 0
fi

cleanup() { rm -rf "$TEMP_DIR" "$KNOWN_HOSTS_FILE"; }
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

# ── 2. deploy binary & workspace (single compressed pipe) ──────────────────

if $RESTART_SVC; then
  echo ":: [2/4] stopping service..."
  $SSH_CMD "$TARGET" "XDG_RUNTIME_DIR=/run/user/\$(id -u) systemctl --user stop aestival-bot.service" 2>&1 || true
  sleep 1
fi

echo ":: [3/4] deploying binary & workspace (tar.gz pipe)..."
# Single compressed transfer: binary + workspace in one shot (~4 MB instead of 13+)
$SSH_CMD "$TARGET" "mkdir -p $REMOTE_BIN/{config,contexts,workspace}"
tar -czf - -C "$(dirname "$BINARY")" aestival -C "$REPO_ROOT/workspace" . |
  $SSH_CMD "$TARGET" "
    tar -xzf - -C $REMOTE_BIN &&
    mv $REMOTE_BIN/aestival $REMOTE_BIN/aestival.new
  "

echo "   binary size: $(du -h "$BINARY" | cut -f1)"

# ── 3. config & contexts check ──────────────────────────────────────────────

echo ":: [4/4] config..."

# Config — seed from template only if none exists on the server.
# Never overwrite an existing config (contains secrets).
$SSH_CMD "$TARGET" "
  if [ ! -f '$REMOTE_BIN/config/bot_config.json' ]; then
    echo '   [seed] config/bot_config.json (first deploy — edit it!)'
  else
    echo '   [keep] config/bot_config.json (already exists)'
  fi
"

echo "   [sync] workspace/ (included in pipe)"
echo "   [keep] contexts/ (untouched)"

# ── 4. finalise (atomic binary swap + optional restart) ──────────────────────

$SSH_CMD "$TARGET" "
  set -e
  mv '$REMOTE_BIN/aestival.new' '$REMOTE_BIN/aestival'
  echo ':: binary atomically replaced'
"

echo ":: deploy finished at $(date '+%F %T')"

if $RESTART_SVC; then
  echo ":: starting aestival-bot.service..."
  $SSH_CMD "$TARGET" "XDG_RUNTIME_DIR=/run/user/\$(id -u) systemctl --user start aestival-bot.service" 2>&1
  echo ""
  echo ":: service status:"
  $SSH_CMD "$TARGET" "XDG_RUNTIME_DIR=/run/user/\$(id -u) systemctl --user --no-pager status aestival-bot.service" 2>&1 || true
else
  echo ""
  echo "  To restart:  ./deploy.sh --restart"
fi
