#!/bin/bash
# Kill every old miner/wrapper, pull origin/main, rebuild, start.
# ONLY run this — do not git fetch first (that prompts for GitHub).
#   bash /workspace/xnminer-low-dif-hybrid-blackwell/scripts/hard-restart.sh
set -u
export GIT_TERMINAL_PROMPT=0
export GIT_ASKPASS=true
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Keep wallet / token from the live wrapper if it exists.
VPID=$(pgrep -f 'bash vast.sh' | head -n1 || true)
if [[ -n "${VPID}" && -r "/proc/${VPID}/environ" ]]; then
  while IFS= read -r line; do
    case "$line" in
      GH_TOKEN=*|GITHUB_TOKEN=*|GIT_TOKEN=*|ADDRESS=*|XEN_ADDRESS=*|WALLET=*|WORKER=*)
        export "$line"
        ;;
    esac
  done < <(tr '\0' '\n' < "/proc/${VPID}/environ")
fi
if [[ -z "${ADDRESS:-}" && -f miner.ini ]]; then
  ADDRESS=$(awk -F' *= *' '/^address/{print $2; exit}' miner.ini)
  export ADDRESS
fi
if [[ -z "${WORKER:-}" && -f miner.ini ]]; then
  WORKER=$(awk -F' *= *' '/^worker/{print $2; exit}' miner.ini)
  export WORKER
fi

echo "stopping old processes on $(hostname)"
pkill -TERM -f /build/bin/xnminer 2>/dev/null || true
sleep 6
pkill -KILL -f /build/bin/xnminer 2>/dev/null || true
pkill -KILL -f 'bash vast.sh' 2>/dev/null || true
pkill -KILL -f /tmp/restart-gpu.sh 2>/dev/null || true
pkill -KILL -f /tmp/remote-update.sh 2>/dev/null || true
sleep 2

if [[ -z "${GH_TOKEN:-}" ]]; then
  echo "ERROR: no GH_TOKEN in this shell or in the live vast.sh process."
  echo "Paste:  export GH_TOKEN=ghp_your_pat"
  echo "Then run this script again. Do not type a GitHub username into git."
  exit 2
fi
git remote set-url origin "https://x-access-token:${GH_TOKEN}@github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"
if ! git fetch --depth 1 origin; then
  echo "ERROR: git fetch failed — token missing or revoked. Do not retry with a username prompt."
  git remote set-url origin "https://github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"
  exit 3
fi
git reset --hard origin/main
git remote set-url origin "https://github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"

echo "now=$(git rev-parse --short HEAD)"
chmod +x vast.sh build.sh scripts/hard-restart.sh scripts/detect-hardware.sh scripts/ensure-cuda13.sh scripts/verify-warp-socks.sh 2>/dev/null || true
exec bash vast.sh
