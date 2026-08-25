#!/usr/bin/env bash
# Vast.ai / any CUDA Linux box: clone (private repo), build, mine.
#
# Do NOT curl raw.githubusercontent.com — private repos return 404 there.
# Paste this (real PAT + real 0x wallet, not the words YOUR_PAT / YOUR_WALLET):
#
#   export GH_TOKEN=ghp_xxxxxxxx ADDRESS=0xYourWallet
#   apt-get update -y && apt-get install -y git ca-certificates
#   git clone --depth 1 "https://x-access-token:${GH_TOKEN}@github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"
#   cd xnminer-low-dif-hybrid-blackwell && bash vast.sh
set -euo pipefail

GH_TOKEN="${GH_TOKEN:-${GITHUB_TOKEN:-${GIT_TOKEN:-}}}"
if [[ -z "${GH_TOKEN}" && -f /root/.config/xnminer/gh_token ]]; then
  GH_TOKEN="$(tr -d ' \r\n' < /root/.config/xnminer/gh_token || true)"
fi
if [[ -z "${GH_TOKEN}" && -f "${HOME}/.config/xnminer/gh_token" ]]; then
  GH_TOKEN="$(tr -d ' \r\n' < "${HOME}/.config/xnminer/gh_token" || true)"
fi
if [[ -n "${GH_TOKEN}" && "${GH_TOKEN}" != "YOUR_PAT" ]]; then
  mkdir -p "${HOME}/.config/xnminer"
  umask 077
  printf '%s' "${GH_TOKEN}" > "${HOME}/.config/xnminer/gh_token" || true
fi
REPO_SLUG="badnob/xnminer-low-dif-hybrid-blackwell"
ADDRESS="${ADDRESS:-${XEN_ADDRESS:-${WALLET:-}}}"
WORKER="${WORKER:-}"
DEVICE="${DEVICE:-0}"
BAG_FORWARD_URL="${BAG_FORWARD_URL:-}"
BAG_FORWARD_TOKEN="${BAG_FORWARD_TOKEN:-}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "${HERE}/CMakeLists.txt" && -f "${HERE}/miner.ini.example" ]]; then
  ROOT="${HERE}"
elif [[ -d /workspace && -w /workspace ]]; then
  ROOT="/workspace/xnminer-low-dif-hybrid-blackwell"
else
  ROOT="${HOME}/xnminer-low-dif-hybrid-blackwell"
fi

if [[ -z "${ADDRESS}" ]]; then
  echo "ERROR: set ADDRESS to your 0x wallet (not the text 0xYOUR_WALLET)." >&2
  exit 1
fi
if [[ ! "${ADDRESS}" =~ ^0[xX][0-9a-fA-F]{40}$ ]]; then
  echo "ERROR: ADDRESS must be 0x + 40 hex chars (got: ${ADDRESS})" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
# Prefer CUDA 13 nvcc (desktop 16 MH/s compiler) over the image's 12.8.
for _cuda in /usr/local/cuda-13.3 /usr/local/cuda-13.2 /usr/local/cuda-13.1 /usr/local/cuda-13.0 /usr/local/cuda; do
  if [[ -x "${_cuda}/bin/nvcc" ]] && "${_cuda}/bin/nvcc" --version 2>/dev/null | grep -q 'release 13'; then
    export PATH="${_cuda}/bin:/usr/lib/nvidia-cuda-toolkit/bin:${PATH:-}"
    export LD_LIBRARY_PATH="${_cuda}/lib64:${LD_LIBRARY_PATH:-}"
    break
  fi
done
if ! command -v nvcc >/dev/null 2>&1; then
  export PATH="/usr/local/cuda/bin:/usr/lib/nvidia-cuda-toolkit/bin:${PATH:-}"
  export LD_LIBRARY_PATH="/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}"
fi

need_pkg() { command -v "$1" >/dev/null 2>&1; }

install_deps() {
  if need_pkg apt-get; then
    apt-get update -y
    apt-get install -y --no-install-recommends \
      git ca-certificates build-essential cmake ninja-build pkg-config \
      libcurl4-openssl-dev
  elif need_pkg dnf; then
    dnf install -y git gcc-c++ cmake ninja-build pkgconf-pkg-config libcurl-devel
  fi
}

if ! need_pkg git || ! need_pkg cmake || ! need_pkg g++ || ! ldconfig -p 2>/dev/null | grep -q libcurl; then
  echo "Installing build deps..."
  install_deps
fi

if ! need_pkg nvcc; then
  echo "ERROR: nvcc not found. Use a Vast.ai CUDA image (devel), not a runtime-only image." >&2
  echo "PATH=${PATH}" >&2
  exit 1
fi

need_clone=1
if [[ -d "${ROOT}/.git" && -f "${ROOT}/CMakeLists.txt" ]]; then
  need_clone=0
fi

if [[ -n "${GH_TOKEN}" && "${GH_TOKEN}" != "YOUR_PAT" ]]; then
  CLONE_URL="https://x-access-token:${GH_TOKEN}@github.com/${REPO_SLUG}.git"
  if [[ "${need_clone}" -eq 1 ]]; then
    echo "Cloning ${REPO_SLUG} -> ${ROOT}"
    rm -rf "${ROOT}"
    git clone --depth 1 "${CLONE_URL}" "${ROOT}"
  else
    echo "Updating ${ROOT}..."
    git -C "${ROOT}" remote set-url origin "${CLONE_URL}"
    git -C "${ROOT}" fetch --depth 1 origin
    git -C "${ROOT}" reset --hard origin/main 2>/dev/null || git -C "${ROOT}" reset --hard origin/master
  fi
  git -C "${ROOT}" remote set-url origin "https://github.com/${REPO_SLUG}.git"
elif [[ "${need_clone}" -eq 1 ]]; then
  echo "ERROR: private repo — export GH_TOKEN to a real GitHub PAT (repo scope)." >&2
  echo "  It must start with ghp_  — not the word YOUR_PAT." >&2
  exit 1
fi

cd "${ROOT}"
chmod +x build.sh start-miner.sh install-deps.sh vast.sh \
  scripts/detect-hardware.sh scripts/ensure-cuda13.sh scripts/hard-restart.sh 2>/dev/null || true

if command -v nvidia-smi >/dev/null 2>&1; then
  nvidia-smi -pm 1 >/dev/null 2>&1 || true
  maxpl="$(nvidia-smi --query-gpu=power.max_limit --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -d '[:space:]')"
  if [[ -n "${maxpl}" ]]; then
    nvidia-smi -pl "${maxpl}" >/dev/null 2>&1 || true
  fi
  maxsm="$(nvidia-smi --query-gpu=clocks.max.sm --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -dc '0-9')"
  maxmem="$(nvidia-smi --query-gpu=clocks.max.mem --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -dc '0-9')"
  if [[ -n "${maxsm}" ]]; then
    nvidia-smi -lgc "${maxsm}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${maxmem}" ]]; then
    nvidia-smi -lmc "${maxmem}" >/dev/null 2>&1 || true
  fi
  nvidia-smi --query-gpu=name,compute_cap,power.limit,power.draw,clocks.sm,clocks.mem --format=csv
fi

export CUDA_DEVICE_MAX_CONNECTIONS="${CUDA_DEVICE_MAX_CONNECTIONS:-32}"

write_build_sha() {
  mkdir -p data
  if git rev-parse HEAD >/dev/null 2>&1; then
    git rev-parse HEAD > data/build_sha
  fi
}

apply_ini_env() {
  # Rewrite only identity / vault keys. Never replace a live miner.ini wholesale.
  if [[ ! -f miner.ini ]]; then
    cp -f miner.ini.example miner.ini
  fi
  tmp="$(mktemp)"
  awk -v addr="${ADDRESS}" -v worker="${WORKER}" -v dev="${DEVICE}" \
      -v bagurl="${BAG_FORWARD_URL}" -v bagtok="${BAG_FORWARD_TOKEN}" '
    /^address[[:space:]]*=/ { print "address = " addr; next }
    /^worker[[:space:]]*=/ {
      if (worker != "") { print "worker = " worker; next }
    }
    /^woodyminer_custom_name[[:space:]]*=/ {
      if (worker != "") { print "woodyminer_custom_name = " worker; next }
    }
    /^device_id[[:space:]]*=/ { print "device_id = " dev; next }
    /^bag_forward_url[[:space:]]*=/ {
      if (bagurl != "") { print "bag_forward_url = " bagurl; next }
    }
    /^bag_forward_token[[:space:]]*=/ {
      if (bagtok != "") { print "bag_forward_token = " bagtok; next }
    }
    /^xuni_mining_enabled[[:space:]]*=/ { print "xuni_mining_enabled = false"; next }
    /^match_drain_parallel[[:space:]]*=/ { print "match_drain_parallel = 0"; next }
    /^submit_enabled[[:space:]]*=/ { print "submit_enabled = true"; se=1; next }
    /^match_drain_enabled[[:space:]]*=/ { print "match_drain_enabled = true"; md=1; next }
    /^send_pow_enabled[[:space:]]*=/ { print "send_pow_enabled = false"; next }
    /^connection_timeout_s[[:space:]]*=/ { print "connection_timeout_s = 20"; next }
    /^network_poll_interval_s[[:space:]]*=/ { print "network_poll_interval_s = 1"; next }
    /^network_poll_timeout_s[[:space:]]*=/ { print "network_poll_timeout_s = 3"; next }
    /^network_down_poll_interval_s[[:space:]]*=/ { print "network_down_poll_interval_s = 1"; next }
    /^lastblock_poll_interval_s[[:space:]]*=/ { print "lastblock_poll_interval_s = 1"; next }
    /^lastblock_poll_ms[[:space:]]*=/ { print "lastblock_poll_ms = 1000"; next }
    /^lastblock_timeout_s[[:space:]]*=/ { print "lastblock_timeout_s = 3"; next }
    /^keygen_threads[[:space:]]*=/ { print "keygen_threads = 0"; next }
    /^max_lanes[[:space:]]*=/ { print "max_lanes = 0"; next }
    /^target_vram_pct[[:space:]]*=/ { print "target_vram_pct = 80.0"; next }
    /^desktop_headroom_pct[[:space:]]*=/ { print "desktop_headroom_pct = 20.0"; next }
    /^emergency_vram_pct[[:space:]]*=/ { print "emergency_vram_pct = 95.0"; next }
    /^max_gpu_temp_c[[:space:]]*=/ { print "max_gpu_temp_c = 85"; next }
    /^warn_gpu_temp_c[[:space:]]*=/ { print "warn_gpu_temp_c = 75"; next }
    /^max_mem_temp_c[[:space:]]*=/ { print "max_mem_temp_c = 85"; next }
    /^warn_mem_temp_c[[:space:]]*=/ { print "warn_mem_temp_c = 81"; next }
    /^gpu_power_boost_enabled[[:space:]]*=/ { print "gpu_power_boost_enabled = false"; next }
    /^gpu_power_target_pct[[:space:]]*=/ { print "gpu_power_target_pct = 100"; next }
    /^gpu_power_min_pct[[:space:]]*=/ { print "gpu_power_min_pct = 100"; next }
    /^gpu_difficulty_power_enabled[[:space:]]*=/ { print "gpu_difficulty_power_enabled = false"; next }
    /^gpu_thermal_batch_min_scale[[:space:]]*=/ { print "gpu_thermal_batch_min_scale = 0.70"; next }
    /^gpu_thermal_start_scale[[:space:]]*=/ { print "gpu_thermal_start_scale = 0.70"; next }
    /^gpu_cooldown_s[[:space:]]*=/ { print "gpu_cooldown_s = 20"; next }
    /^desktop_cpu_cores[[:space:]]*=/ { print "desktop_cpu_cores = 0"; next }
    /^bag_sort_cpu_cores[[:space:]]*=/ { print "bag_sort_cpu_cores = 0"; next }
    /^flush_cpu_cores[[:space:]]*=/ { print "flush_cpu_cores = 0"; next }
    /^dashboard_cpu_cores[[:space:]]*=/ { print "dashboard_cpu_cores = 0"; next }
    { print }
    END {
      if (!se) print "submit_enabled = true"
      if (!md) print "match_drain_enabled = true"
    }
  ' miner.ini > "${tmp}"
  mv "${tmp}" miner.ini
}

apply_git_update() {
  if [[ -z "${GH_TOKEN}" || "${GH_TOKEN}" == "YOUR_PAT" ]]; then
    return 1
  fi
  CLONE_URL="https://x-access-token:${GH_TOKEN}@github.com/${REPO_SLUG}.git"
  git remote set-url origin "${CLONE_URL}"
  git fetch --depth 1 origin
  branch="main"
  git rev-parse --verify origin/main >/dev/null 2>&1 || branch="master"
  local_sha="$(git rev-parse HEAD)"
  remote_sha="$(git rev-parse "origin/${branch}")"
  built_sha="$(tr -d ' \r\n' < data/build_sha 2>/dev/null || true)"
  git remote set-url origin "https://github.com/${REPO_SLUG}.git"
  if [[ "${local_sha}" == "${remote_sha}" && "${built_sha}" == "${remote_sha}" ]]; then
    echo "already up to date (${local_sha:0:8})"
    return 1
  fi
  echo "GitHub update ${local_sha:0:8} -> ${remote_sha:0:8} (built ${built_sha:0:8}) — rebuilding"
  git reset --hard "origin/${branch}"
  chmod +x build.sh start-miner.sh install-deps.sh vast.sh scripts/ensure-cuda13.sh scripts/detect-hardware.sh 2>/dev/null || true
  ./build.sh
  write_build_sha
  apply_ini_env
  return 0
}

github_remote_sha() {
  if [[ -z "${GH_TOKEN}" ]]; then
    return 1
  fi
  # git fetch/ls-remote — the REST commits API 403s under watcher load.
  git ls-remote "https://x-access-token:${GH_TOKEN}@github.com/${REPO_SLUG}.git" refs/heads/main 2>/dev/null | awk '{print $1}'
}

echo "Building for this GPU / CPU / VRAM..."
./build.sh
write_build_sha
apply_ini_env

echo
echo "Starting miner  address=${ADDRESS}  worker=${WORKER:-auto}  device=${DEVICE}  bag=${BAG_FORWARD_URL:-off}"
echo "Logs: ${ROOT}/data/session.log"
echo "TUI:  tmux attach -t xnminer     (detach: Ctrl+B then D — miner keeps running)"
echo "Auto-update: GitHub ${REPO_SLUG} (SIGTERM bags the queue, then pull + rebuild + restart)"
echo

mkdir -p data
if ! command -v tmux >/dev/null 2>&1; then
  apt-get update -y >/dev/null 2>&1 || true
  apt-get install -y tmux >/dev/null 2>&1 || true
fi
stopping=0
miner_pid=""
trap 'stopping=1; echo; echo "stop requested — not restarting"; if [[ -n "${miner_pid}" ]]; then kill -TERM "${miner_pid}" 2>/dev/null || true; fi' INT TERM

launch_tui() {
  tmux has-session -t xnminer 2>/dev/null && tmux kill-session -t xnminer 2>/dev/null || true
  # Hide tmux chrome and force Campbell colors so SSH matches the desktop TUI.
  tmux new-session -d -s xnminer -x 82 -y 46 \
    "cd '${ROOT}' && export TERM=xterm-256color COLORTERM=truecolor && exec ./build/bin/xnminer"
  tmux set-option -t xnminer status off
  tmux set-option -t xnminer set-titles off
  tmux set-option -t xnminer pane-border-status off
  tmux set-option -t xnminer window-style 'bg=#0C0C0C,fg=#CCCCCC'
  tmux set-option -t xnminer window-active-style 'bg=#0C0C0C,fg=#CCCCCC'
  tmux set-option -t xnminer -ga terminal-overrides ',*:RGB'
  tmux set-option -t xnminer -ga terminal-overrides ',*:Tc'
}

while [[ "${stopping}" -eq 0 ]]; do
  set +e
  launch_tui
  sleep 2
  miner_pid="$(pgrep -x xnminer | head -n1 || true)"
  if [[ -z "${miner_pid}" ]]; then
    echo "miner failed to start in tmux"
    sleep 3
    continue
  fi
  (
    while kill -0 "${miner_pid}" 2>/dev/null; do
      sleep 300
      remote="$(github_remote_sha || true)"
      remote="${remote//$'\r'/}"
      remote="${remote//$'\n'/}"
      local_sha="$(tr -d ' \r\n' < data/build_sha 2>/dev/null || git rev-parse HEAD 2>/dev/null || true)"
      if [[ -n "${remote}" && -n "${local_sha}" && "${remote}" != "${local_sha}" ]]; then
        echo "wrapper saw GitHub ${remote:0:8} (have ${local_sha:0:8}) — signaling miner to bag and exit"
        kill -TERM "${miner_pid}" 2>/dev/null || true
        break
      fi
    done
  ) &
  watcher_pid=$!
  while kill -0 "${miner_pid}" 2>/dev/null; do
    sleep 2
  done
  rc=0
  miner_pid=""
  kill "${watcher_pid}" 2>/dev/null || true
  wait "${watcher_pid}" 2>/dev/null || true
  set -e
  if [[ "${stopping}" -ne 0 ]]; then
    break
  fi
  if [[ "${rc}" -eq 75 ]]; then
    echo "miner requested update (rc=75)"
  else
    echo "miner exited rc=${rc}"
  fi
  apply_git_update || true
  echo "restarting in 3s (Ctrl+C to stop)"
  sleep 3
done
echo "miner stopped."
