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
#
# bash vast.sh always opens the miner dashboard in THIS window.
# SOCKS / auto-update run in a background tmux session (xnwrap) so they
# cannot steal the screen. SSH drop does not stop mining. Run bash vast.sh
# again after reconnect to see the dashboard. Detach: Ctrl+B then D.
#
# Internal: bash vast.sh --watch  (restart loop; do not run by hand)
set -euo pipefail

WATCH=0
if [[ "${1:-}" == "--watch" ]]; then
  WATCH=1
  shift
fi

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
VERIFY_PROXY="${VERIFY_PROXY:-}"
SUBMIT_ENABLED="${SUBMIT_ENABLED:-true}"
# Empty = follow miner.ini verify_warp_socks (default true). 0/1 overrides the file.
WARP_SOCKS="${WARP_SOCKS:-}"
# Hybrid CUDA m=. Old miner.ini still says 100; pin here so a new binary/env wins
# even when the in-memory vast.sh --watch apply_ini_env is from the previous commit.
export FORCE_MINE_MEMORY_COST="${FORCE_MINE_MEMORY_COST:-10000}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "${HERE}/CMakeLists.txt" && -f "${HERE}/miner.ini.example" ]]; then
  ROOT="${HERE}"
elif [[ -d /workspace && -w /workspace ]]; then
  ROOT="/workspace/xnminer-low-dif-hybrid-blackwell"
else
  ROOT="${HOME}/xnminer-low-dif-hybrid-blackwell"
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
      libcurl4-openssl-dev tmux
  elif need_pkg dnf; then
    dnf install -y git gcc-c++ cmake ninja-build pkgconf-pkg-config libcurl-devel tmux
  fi
}

load_ini_identity() {
  local ini="${ROOT}/miner.ini"
  [[ -f "${ini}" ]] || return 0
  if [[ -z "${ADDRESS}" ]]; then
    ADDRESS="$(awk -F= '/^[[:space:]]*address[[:space:]]*=/{v=$2; gsub(/[ \t\r]/,"",v); print v; exit}' "${ini}" || true)"
  fi
  if [[ -z "${WORKER}" ]]; then
    WORKER="$(awk -F= '/^[[:space:]]*worker[[:space:]]*=/{v=$2; gsub(/[ \t\r]/,"",v); print v; exit}' "${ini}" || true)"
  fi
}

require_address() {
  if [[ -z "${ADDRESS}" ]]; then
    echo "ERROR: set ADDRESS to your 0x wallet (not the text 0xYOUR_WALLET)." >&2
    exit 1
  fi
  if [[ ! "${ADDRESS}" =~ ^0[xX][0-9a-fA-F]{40}$ ]]; then
    echo "ERROR: ADDRESS must be 0x + 40 hex chars (got: ${ADDRESS})" >&2
    exit 1
  fi
}

ensure_worker_name() {
  if [[ -n "${WORKER}" ]]; then
    return 0
  fi
  local host
  host="$(hostname 2>/dev/null | tr -cd 'a-zA-Z0-9' || true)"
  if [[ -n "${host}" ]]; then
    WORKER="vast-${host:0:12}"
  else
    WORKER="vast-${DEVICE}-$$"
  fi
}

ensure_tmux() {
  if need_pkg tmux; then
    return 0
  fi
  echo "Installing tmux..."
  if need_pkg apt-get; then
    apt-get update -y >/dev/null 2>&1 || true
    apt-get install -y tmux >/dev/null 2>&1 || true
  elif need_pkg dnf; then
    dnf install -y tmux >/dev/null 2>&1 || true
  fi
  if ! need_pkg tmux; then
    echo "ERROR: tmux is required so the miner screen survives SSH disconnect." >&2
    exit 1
  fi
}

run_nvsmi() {
  if command -v timeout >/dev/null 2>&1; then
    timeout 8 nvidia-smi "$@"
  else
    nvidia-smi "$@"
  fi
}

miner_alive() {
  tmux has-session -t xnminer 2>/dev/null && pgrep -x xnminer >/dev/null 2>&1
}

wrap_alive() {
  tmux has-session -t xnwrap 2>/dev/null && pgrep -f 'vast.sh --watch' >/dev/null 2>&1
}

ini_force_mine_m() {
  awk -F= '/^[[:space:]]*force_mine_memory_cost[[:space:]]*=/{v=$2; gsub(/[ \t\r]/,"",v); print v; exit}' \
    "${ROOT}/miner.ini" 2>/dev/null || true
}

running_mine_m() {
  tr -d ' \r\n' < "${ROOT}/data/running_mine_m" 2>/dev/null || true
}

session_mine_m() {
  # Last "CUDA mine m=N fixed" line in session.log.
  awk '/CUDA mine m=[0-9]+ fixed/{m=$0} END{
    if (m ~ /CUDA mine m=([0-9]+) fixed/) {
      sub(/.*CUDA mine m=/, "", m)
      sub(/ fixed.*/, "", m)
      print m
    }
  }' "${ROOT}/data/session.log" 2>/dev/null || true
}

stop_miner_process() {
  pkill -TERM -x xnminer 2>/dev/null || true
  local i=0
  while [[ "${i}" -lt 20 ]] && pgrep -x xnminer >/dev/null 2>&1; do
    sleep 1
    i=$((i + 1))
  done
  pkill -KILL -x xnminer 2>/dev/null || true
}

# Restart a live miner when it is still hashing the retired hybrid m=100.
bounce_miner_if_wrong_m() {
  local want="${FORCE_MINE_MEMORY_COST:-10000}"
  local have ini logged seen
  have="$(running_mine_m)"
  ini="$(ini_force_mine_m)"
  logged="$(session_mine_m)"
  seen="${have:-${logged}}"
  if ! miner_alive && ! pgrep -x xnminer >/dev/null 2>&1; then
    return 0
  fi
  if [[ "${seen}" == "${want}" && "${ini}" == "${want}" ]]; then
    return 0
  fi
  # No stamp/log yet (just started) and ini already pinned — do not loop.
  if [[ -z "${seen}" && "${ini}" == "${want}" ]]; then
    return 0
  fi
  echo "Hybrid mine m= process=${have:-unknown} log=${logged:-unknown} ini=${ini:-unknown} want=${want} — bouncing miner"
  stop_miner_process
}

show_miner_screen() {
  echo
  echo "Opening the miner dashboard in this window."
  echo "Mining keeps running if you disconnect. Come back with:  bash vast.sh"
  echo "Leave the screen without stopping:  Ctrl+B  then  D"
  echo
  if [[ -n "${TMUX:-}" ]]; then
    if tmux switch-client -t xnminer 2>/dev/null; then
      return 0
    fi
    echo "Could not switch tmux client — trying a nested attach." >&2
    TMUX='' tmux attach -t xnminer
    return $?
  fi
  tmux attach -t xnminer
}

wait_for_miner_screen() {
  local i=0
  echo "Starting proxy + miner in the background. Dashboard opens here when ready..."
  while [[ "${i}" -lt 90 ]]; do
    if miner_alive; then
      return 0
    fi
    i=$((i + 1))
    if (( i % 5 == 0 )); then
      echo "  still starting... ${i}s (SOCKS/GPU, not stuck)"
    fi
    sleep 1
  done
  echo "ERROR: miner dashboard did not start within 90s." >&2
  echo "----- wrapper log -----" >&2
  tail -n 80 "${ROOT}/data/wrapper.log" 2>/dev/null || true
  echo "----- wrap pane -----" >&2
  tmux capture-pane -t xnwrap -p -S -100 2>/dev/null || true
  echo "----- session.log -----" >&2
  tail -n 40 "${ROOT}/data/session.log" 2>/dev/null || true
  return 1
}

start_wrap() {
  mkdir -p "${ROOT}/data" "${HOME}/.config/xnminer"
  if wrap_alive; then
    return 0
  fi
  tmux has-session -t xnwrap 2>/dev/null && tmux kill-session -t xnwrap 2>/dev/null || true
  local envf="${HOME}/.config/xnminer/vast.env"
  umask 077
  {
    printf 'export GH_TOKEN=%q\n' "${GH_TOKEN:-}"
    printf 'export ADDRESS=%q\n' "${ADDRESS:-}"
    printf 'export WORKER=%q\n' "${WORKER:-}"
    printf 'export DEVICE=%q\n' "${DEVICE:-0}"
    printf 'export BAG_FORWARD_URL=%q\n' "${BAG_FORWARD_URL:-}"
    printf 'export BAG_FORWARD_TOKEN=%q\n' "${BAG_FORWARD_TOKEN:-}"
    printf 'export VERIFY_PROXY=%q\n' "${VERIFY_PROXY:-}"
    printf 'export SUBMIT_ENABLED=%q\n' "${SUBMIT_ENABLED:-true}"
    printf 'export WARP_SOCKS=%q\n' "${WARP_SOCKS:-}"
    printf 'export MATCH_DRAIN_PARALLEL=%q\n' "${MATCH_DRAIN_PARALLEL:-}"
    printf 'export MATCH_DRAIN_BATCH=%q\n' "${MATCH_DRAIN_BATCH:-}"
    printf 'export FORCE_MINE_MEMORY_COST=%q\n' "${FORCE_MINE_MEMORY_COST:-10000}"
  } > "${envf}"
  tmux new-session -d -s xnwrap -n wrap \
    "set -a; . '${envf}'; set +a; cd '${ROOT}'; exec bash '${ROOT}/vast.sh' --watch"
}

setup_verify_warp() {
  mkdir -p "${ROOT}/data/warp-socks"
  if [[ ! -f miner.ini && -f miner.ini.example ]]; then
    cp -f miner.ini.example miner.ini
  fi
  local flag=""
  if [[ -f miner.ini ]]; then
    flag="$(awk -F= '/^[[:space:]]*verify_warp_socks[[:space:]]*=/{v=$2; gsub(/[ \t\r]/,"",v); print v; exit}' miner.ini || true)"
  fi
  [[ -z "${flag}" ]] && flag=true
  case "${WARP_SOCKS,,}" in
    0|false|no|off) flag=false ;;
    1|true|yes|on) flag=true ;;
  esac
  if [[ -n "${VERIFY_PROXY}" && "${VERIFY_PROXY}" != "socks5h://127.0.0.1:40000" ]]; then
    echo "Using custom VERIFY_PROXY (WARP SOCKS skipped)."
    return 0
  fi
  if [[ "${flag,,}" == "false" || "${flag,,}" == "0" || "${flag,,}" == "off" || "${flag,,}" == "no" ]]; then
    rm -f "${ROOT}/data/warp-socks/enabled"
    VERIFY_PROXY=""
    echo "verify_warp_socks is off — POST /verify uses this box IP."
    return 0
  fi
  touch "${ROOT}/data/warp-socks/enabled"
  if bash "${ROOT}/scripts/verify-warp-socks.sh"; then
    VERIFY_PROXY="socks5h://127.0.0.1:40000"
  else
    echo "WARN: WARP SOCKS failed — POST /verify will use this box IP. SSH is fine." >&2
    VERIFY_PROXY=""
  fi
}

gpu_once() {
  if ! command -v nvidia-smi >/dev/null 2>&1; then
    echo "nvidia-smi not in PATH — skipping GPU clock lock."
    return 0
  fi
  echo "Checking GPU (each nvidia-smi call limited to 8s)..."
  echo "GPU kernel driver is the Vast host module — not replaced on git push."
  run_nvsmi --query-gpu=name,driver_version,compute_cap --format=csv || true
  run_nvsmi -pm 1 >/dev/null 2>&1 || true
  local maxpl maxsm maxmem
  maxpl="$(run_nvsmi --query-gpu=power.max_limit --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"
  if [[ -n "${maxpl}" ]]; then
    run_nvsmi -pl "${maxpl}" >/dev/null 2>&1 || true
  fi
  maxsm="$(run_nvsmi --query-gpu=clocks.max.sm --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -dc '0-9' || true)"
  maxmem="$(run_nvsmi --query-gpu=clocks.max.mem --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -dc '0-9' || true)"
  if [[ -n "${maxsm}" ]]; then
    run_nvsmi -lgc "${maxsm}" >/dev/null 2>&1 || echo "GPU SM clock lock skipped (nvidia-smi timed out or denied)."
  fi
  if [[ -n "${maxmem}" ]]; then
    run_nvsmi -lmc "${maxmem}" >/dev/null 2>&1 || echo "GPU mem clock lock skipped (nvidia-smi timed out or denied)."
  fi
  run_nvsmi --query-gpu=name,compute_cap,power.limit,power.draw,clocks.sm,clocks.mem --format=csv || true
}

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
      -v bagurl="${BAG_FORWARD_URL}" -v bagtok="${BAG_FORWARD_TOKEN}" \
      -v vproxy="${VERIFY_PROXY}" -v suben="${SUBMIT_ENABLED}" \
      -v vwarp="${WARP_SOCKS}" \
      -v mpar="${MATCH_DRAIN_PARALLEL}" -v mbat="${MATCH_DRAIN_BATCH}" '
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
    /^verify_proxy[[:space:]]*=/ {
      if (vproxy != "") { print "verify_proxy = " vproxy; vp=1; next }
    }
    /^verify_warp_socks[[:space:]]*=/ {
      vw=1
      if (vwarp == "0" || vwarp == "false" || vwarp == "off" || vwarp == "no") {
        print "verify_warp_socks = false"; next
      }
      if (vwarp == "1" || vwarp == "true" || vwarp == "on" || vwarp == "yes") {
        print "verify_warp_socks = true"; next
      }
    }
    /^xuni_mining_enabled[[:space:]]*=/ { print "xuni_mining_enabled = false"; next }
    /^[[:space:]]*force_mine_memory_cost[[:space:]]*=/ { print "force_mine_memory_cost = 10000"; fm=1; next }
    /^match_drain_parallel[[:space:]]*=/ {
      if (mpar != "") { print "match_drain_parallel = " mpar; next }
    }
    /^match_drain_batch[[:space:]]*=/ {
      if (mbat != "") { print "match_drain_batch = " mbat; next }
    }
    /^submit_enabled[[:space:]]*=/ { print "submit_enabled = " suben; se=1; next }
    /^match_drain_enabled[[:space:]]*=/ { print "match_drain_enabled = true"; md=1; next }
    /^send_pow_enabled[[:space:]]*=/ { print "send_pow_enabled = false"; next }
    /^connection_timeout_s[[:space:]]*=/ { print "connection_timeout_s = 20"; next }
    /^network_poll_interval_s[[:space:]]*=/ { print "network_poll_interval_s = 1"; next }
    /^network_poll_timeout_s[[:space:]]*=/ { print "network_poll_timeout_s = 3"; next }
    /^network_down_poll_interval_s[[:space:]]*=/ { print "network_down_poll_interval_s = 1"; next }
    /^lastblock_url[[:space:]]*=/ { next }
    /^lastblock_url_fallback[[:space:]]*=/ { next }
    /^lastblock_poll_interval_s[[:space:]]*=/ { next }
    /^lastblock_poll_ms[[:space:]]*=/ { next }
    /^lastblock_timeout_s[[:space:]]*=/ { next }
    /^keygen_threads[[:space:]]*=/ { print "keygen_threads = 12"; next }
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
    /^flush_cpu_cores[[:space:]]*=/ { print "flush_cpu_cores = 6"; next }
    /^dashboard_cpu_cores[[:space:]]*=/ { print "dashboard_cpu_cores = 0"; next }
    { print }
    END {
      if (!se) print "submit_enabled = " suben
      if (!md) print "match_drain_enabled = true"
      if (!fm) print "force_mine_memory_cost = 10000"
      if (vproxy != "" && !vp) print "verify_proxy = " vproxy
      if (!vw) print "verify_warp_socks = true"
    }
  ' miner.ini > "${tmp}"
  mv "${tmp}" miner.ini
}

git_fetch_origin() {
  # Shallow clones on Vast race "shallow file has changed" — retry, never prompt.
  export GIT_TERMINAL_PROMPT=0
  export GIT_ASKPASS=true
  mkdir -p "${ROOT}/data"
  local err="${ROOT}/data/git-fetch.err"
  if GIT_TERMINAL_PROMPT=0 git -C "${ROOT}" -c gc.auto=0 fetch --depth 1 origin >"${err}" 2>&1; then
    return 0
  fi
  if grep -q 'shallow file has changed' "${err}" 2>/dev/null; then
    git -C "${ROOT}" update-ref -d FETCH_HEAD >/dev/null 2>&1 || true
    rm -f "${ROOT}/.git/shallow.lock"
    if GIT_TERMINAL_PROMPT=0 git -C "${ROOT}" -c gc.auto=0 fetch --depth 1 origin >"${err}" 2>&1; then
      return 0
    fi
  fi
  cat "${err}" >&2 || true
  return 1
}

apply_git_update() {
  if [[ -z "${GH_TOKEN}" || "${GH_TOKEN}" == "YOUR_PAT" ]]; then
    return 1
  fi
  CLONE_URL="https://x-access-token:${GH_TOKEN}@github.com/${REPO_SLUG}.git"
  git -C "${ROOT}" remote set-url origin "${CLONE_URL}"
  git_fetch_origin || true
  branch="main"
  git -C "${ROOT}" rev-parse --verify origin/main >/dev/null 2>&1 || branch="master"
  local_sha="$(git -C "${ROOT}" rev-parse HEAD)"
  remote_sha="$(git -C "${ROOT}" rev-parse "origin/${branch}")"
  built_sha="$(tr -d ' \r\n' < "${ROOT}/data/build_sha" 2>/dev/null || true)"
  git -C "${ROOT}" remote set-url origin "https://github.com/${REPO_SLUG}.git"
  if [[ "${local_sha}" == "${remote_sha}" && "${built_sha}" == "${remote_sha}" ]]; then
    echo "already up to date (${local_sha:0:8})"
    return 1
  fi
  echo "GitHub update ${local_sha:0:8} -> ${remote_sha:0:8} (built ${built_sha:0:8}) — rebuilding"
  git -C "${ROOT}" reset --hard "origin/${branch}"
  chmod +x build.sh start-miner.sh install-deps.sh vast.sh \
    scripts/ensure-cuda13.sh scripts/detect-hardware.sh \
    scripts/verify-warp-socks.sh scripts/hard-restart.sh 2>/dev/null || true
  ./build.sh
  write_build_sha
  apply_ini_env
  # Running --watch still has the *previous* bash functions. Re-exec so the
  # new pin / bounce logic actually runs after this git update.
  if [[ "${WATCH}" -eq 1 ]]; then
    echo "re-exec watch with updated vast.sh"
    exec bash "${ROOT}/vast.sh" --watch
  fi
  return 0
}

github_remote_sha() {
  if [[ -z "${GH_TOKEN}" ]]; then
    return 1
  fi
  git ls-remote "https://x-access-token:${GH_TOKEN}@github.com/${REPO_SLUG}.git" refs/heads/main 2>/dev/null | awk '{print $1}'
}

style_miner_session() {
  tmux set-option -t xnminer status off
  tmux set-option -t xnminer set-titles off
  tmux set-option -t xnminer pane-border-status off
  tmux set-option -t xnminer detach-on-destroy off
  tmux set-window-option -t xnminer:0 remain-on-exit on
  tmux set-window-option -t xnminer:0 aggressive-resize on
  tmux set-option -t xnminer window-style 'bg=#0C0C0C,fg=#CCCCCC'
  tmux set-option -t xnminer window-active-style 'bg=#0C0C0C,fg=#CCCCCC'
  tmux set-option -t xnminer -ga terminal-overrides ',*:RGB'
  tmux set-option -t xnminer -ga terminal-overrides ',*:Tc'
}

launch_tui() {
  mkdir -p "${ROOT}/data"
  local cmd="cd '${ROOT}' && export TERM=xterm-256color COLORTERM=truecolor && ./build/bin/xnminer; echo \$? > '${ROOT}/data/last_miner_rc'"
  if tmux has-session -t xnminer 2>/dev/null; then
    tmux respawn-pane -k -t xnminer:0.0 "${cmd}" 2>/dev/null \
      || tmux respawn-pane -k -t xnminer:miner.0 "${cmd}"
  else
    tmux new-session -d -s xnminer -n miner -x 82 -y 46 "${cmd}"
    style_miner_session
  fi
}

read_miner_rc() {
  local rc
  rc="$(tr -d ' \r\n' < "${ROOT}/data/last_miner_rc" 2>/dev/null || true)"
  if [[ "${rc}" =~ ^[0-9]+$ ]]; then
    echo "${rc}"
  else
    echo "1"
  fi
}

# ---------------------------------------------------------------------------
# --watch: background restart loop (tmux session xnwrap). Never attaches.
# ---------------------------------------------------------------------------
if [[ "${WATCH}" -eq 1 ]]; then
  cd "${ROOT}"
  mkdir -p data
  exec >>data/wrapper.log 2>&1
  echo
  echo "===== watch $(date -Is 2>/dev/null || date) ====="
  load_ini_identity
  require_address
  ensure_worker_name
  MATCH_DRAIN_PARALLEL="${MATCH_DRAIN_PARALLEL:-}"
  MATCH_DRAIN_BATCH="${MATCH_DRAIN_BATCH:-}"
  FORCE_MINE_MEMORY_COST="${FORCE_MINE_MEMORY_COST:-10000}"
  if [[ ! -x "${ROOT}/build/bin/xnminer" ]]; then
    echo "Building for this GPU / CPU / VRAM..."
    ./build.sh
    write_build_sha
  fi
  apply_ini_env
  stopping=0
  miner_pid=""
  trap 'stopping=1; echo "watch stop requested"; if [[ -n "${miner_pid}" ]]; then kill -TERM "${miner_pid}" 2>/dev/null || true; fi' INT TERM
  while [[ "${stopping}" -eq 0 ]]; do
    set +e
    echo "watch: ensuring WARP SOCKS..."
    setup_verify_warp
    apply_ini_env
    bounce_miner_if_wrong_m
    miner_pid="$(pgrep -x xnminer | head -n1 || true)"
    if [[ -z "${miner_pid}" ]] || ! tmux has-session -t xnminer 2>/dev/null; then
      echo "watch: launching miner dashboard session"
      launch_tui
      sleep 2
      miner_pid="$(pgrep -x xnminer | head -n1 || true)"
    else
      echo "watch: miner already running pid=${miner_pid}"
    fi
    if [[ -z "${miner_pid}" ]]; then
      echo "miner failed to start in tmux (see session.log / last_miner_rc)"
      sleep 5
      continue
    fi
    echo "watch: miner pid=${miner_pid}  dashboard session=xnminer"
    (
      while kill -0 "${miner_pid}" 2>/dev/null; do
        sleep 60
        remote="$(github_remote_sha || true)"
        remote="${remote//$'\r'/}"
        remote="${remote//$'\n'/}"
        local_sha="$(tr -d ' \r\n' < data/build_sha 2>/dev/null || git rev-parse HEAD 2>/dev/null || true)"
        if [[ -n "${remote}" && -n "${local_sha}" && "${remote}" != "${local_sha}" ]]; then
          echo "wrapper saw GitHub ${remote:0:8} (have ${local_sha:0:8}) — signaling miner to bag and exit"
          kill -TERM "${miner_pid}" 2>/dev/null || true
          break
        fi
        have_m="$(tr -d ' \r\n' < data/running_mine_m 2>/dev/null || true)"
        if [[ -n "${have_m}" && "${have_m}" != "${FORCE_MINE_MEMORY_COST:-10000}" ]]; then
          echo "wrapper saw running mine m=${have_m} (want ${FORCE_MINE_MEMORY_COST:-10000}) — restarting"
          kill -TERM "${miner_pid}" 2>/dev/null || true
          break
        fi
      done
    ) &
    watcher_pid=$!
    while kill -0 "${miner_pid}" 2>/dev/null; do
      sleep 2
    done
    rc="$(read_miner_rc)"
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
    echo "restarting miner in 3s"
    sleep 3
  done
  echo "watch stopped."
  exit 0
fi

# ---------------------------------------------------------------------------
# User-facing: build if needed, then always open the miner dashboard.
# ---------------------------------------------------------------------------
ensure_tmux
cd "${ROOT}" 2>/dev/null || true
load_ini_identity

if miner_alive; then
  want_m="${FORCE_MINE_MEMORY_COST:-10000}"
  before_m="$(ini_force_mine_m)"
  apply_ini_env
  have_m="$(running_mine_m)"
  after_m="$(ini_force_mine_m)"
  logged_m="$(session_mine_m)"
  seen_m="${have_m:-${logged_m}}"
  need_bounce=0
  if [[ "${before_m}" != "${want_m}" || "${after_m}" != "${want_m}" ]]; then
    need_bounce=1
  fi
  if [[ -n "${seen_m}" && "${seen_m}" != "${want_m}" ]]; then
    need_bounce=1
  fi
  if [[ "${need_bounce}" -eq 1 ]]; then
    echo "Miner is up at m=${have_m:-unknown} (ini ${before_m:-?} -> ${after_m:-?}) — bouncing to m=${want_m}."
    stop_miner_process
    if ! wrap_alive; then
      start_wrap
    fi
    wait_for_miner_screen
    show_miner_screen
    exit 0
  fi
  if ! wrap_alive; then
    echo "Miner is up but the restarter was not — starting it in the background."
    start_wrap
  fi
  show_miner_screen
  exit 0
fi

if wrap_alive; then
  echo "Miner is already starting in the background (proxy/GPU). Waiting for the dashboard..."
  wait_for_miner_screen
  show_miner_screen
  exit 0
fi

require_address
ensure_worker_name

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
    git_fetch_origin || git -C "${ROOT}" fetch --depth 1 origin || true
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
  scripts/detect-hardware.sh scripts/ensure-cuda13.sh scripts/hard-restart.sh \
  scripts/verify-warp-socks.sh scripts/verify-proxy-exit.sh 2>/dev/null || true

# Empty = leave miner.ini match_drain_* alone (default 512 in the example).
MATCH_DRAIN_PARALLEL="${MATCH_DRAIN_PARALLEL:-}"
MATCH_DRAIN_BATCH="${MATCH_DRAIN_BATCH:-}"

gpu_once

echo "Building for this GPU / CPU / VRAM..."
./build.sh
write_build_sha
apply_ini_env

echo
echo "Starting miner  address=${ADDRESS}  worker=${WORKER:-auto}  device=${DEVICE}  bag=${BAG_FORWARD_URL:-off}"
echo "Logs: ${ROOT}/data/session.log   wrapper: ${ROOT}/data/wrapper.log"
echo "WARP SOCKS and GitHub auto-update stay in the background. This window becomes the dashboard."
echo

start_wrap
wait_for_miner_screen
show_miner_screen
