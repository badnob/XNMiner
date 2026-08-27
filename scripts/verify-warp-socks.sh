#!/usr/bin/env bash
# Userspace Cloudflare WARP as a local SOCKS5. Not a VPN.
# Does not touch the default route, TUN, or SSH. Only apps that set
# VERIFY_PROXY=socks5h://127.0.0.1:40000 send traffic through it.
set -euo pipefail

PORT="${WARP_SOCKS_PORT:-40000}"
BIND="127.0.0.1:${PORT}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"
DIR="${ROOT}/data/warp-socks"
mkdir -p "${DIR}"
cd "${DIR}"

arch="$(uname -m)"
case "${arch}" in
  x86_64|amd64) goarch=amd64 ;;
  aarch64|arm64) goarch=arm64 ;;
  *)
    echo "ERROR: unsupported arch ${arch} (need x86_64 or arm64)" >&2
    exit 1
    ;;
esac

WGCF_VER="${WGCF_VER:-2.2.32}"
WIREPROXY_VER="${WIREPROXY_VER:-1.1.3}"
WGCF_BIN="${DIR}/wgcf"
WIREPROXY_BIN="${DIR}/wireproxy"

download() {
  local url="$1" out="$2"
  curl -fsSL --retry 3 --retry-delay 2 -o "${out}" "${url}"
}

if [[ ! -x "${WGCF_BIN}" ]]; then
  echo "Downloading wgcf ${WGCF_VER} (${goarch})..."
  download \
    "https://github.com/ViRb3/wgcf/releases/download/v${WGCF_VER}/wgcf_${WGCF_VER}_linux_${goarch}" \
    "${WGCF_BIN}"
  chmod +x "${WGCF_BIN}"
fi

if [[ ! -x "${WIREPROXY_BIN}" ]]; then
  echo "Downloading wireproxy ${WIREPROXY_VER} (${goarch})..."
  tmp="$(mktemp -d)"
  download \
    "https://github.com/windtf/wireproxy/releases/download/v${WIREPROXY_VER}/wireproxy_linux_${goarch}.tar.gz" \
    "${tmp}/wireproxy.tar.gz"
  tar -xzf "${tmp}/wireproxy.tar.gz" -C "${tmp}"
  wp="$(find "${tmp}" -type f -name wireproxy | head -n1)"
  if [[ -z "${wp}" ]]; then
    echo "ERROR: wireproxy binary missing from archive" >&2
    exit 1
  fi
  install -m 0755 "${wp}" "${WIREPROXY_BIN}"
  rm -rf "${tmp}"
fi

if [[ ! -f "${DIR}/wgcf-account.toml" ]]; then
  echo "Registering a free WARP identity (one time)..."
  "${WGCF_BIN}" register --accept-tos
fi
"${WGCF_BIN}" generate >/dev/null

# wireproxy reads a WireGuard conf plus a SOCKS bind. Never wg-quick / never 0.0.0.0/0 on the host.
{
  cat "${DIR}/wgcf-profile.conf"
  printf '\n[Socks5]\nBindAddress = %s\n' "${BIND}"
} > "${DIR}/wireproxy.conf"

socks_up() {
  curl -4 -fsS --max-time 8 -x "socks5h://${BIND}" https://www.cloudflare.com/cdn-cgi/trace 2>/dev/null \
    | grep -qE 'warp=(on|plus)'
}

if socks_up; then
  echo "WARP SOCKS already up on ${BIND}"
else
  if [[ -f "${DIR}/wireproxy.pid" ]] && kill -0 "$(cat "${DIR}/wireproxy.pid")" 2>/dev/null; then
    kill "$(cat "${DIR}/wireproxy.pid")" 2>/dev/null || true
    sleep 1
  fi
  echo "Starting wireproxy SOCKS on ${BIND} (userspace — host route table unchanged)..."
  nohup "${WIREPROXY_BIN}" -c "${DIR}/wireproxy.conf" >"${DIR}/wireproxy.log" 2>&1 &
  echo $! > "${DIR}/wireproxy.pid"
  ok=0
  for _ in $(seq 1 20); do
    sleep 1
    if socks_up; then
      ok=1
      break
    fi
  done
  if [[ "${ok}" -ne 1 ]]; then
    echo "ERROR: WARP SOCKS did not come up. Last log:" >&2
    tail -n 40 "${DIR}/wireproxy.log" >&2 || true
    echo "SSH is still on the Vast IP. Miner will not use a proxy." >&2
    exit 1
  fi
fi

egress="$(curl -4 -fsS --max-time 10 -x "socks5h://${BIND}" https://api.ipify.org || true)"
echo
echo "Local /verify SOCKS is up (not a VPN, SSH untouched)."
echo "  VERIFY_PROXY=socks5h://${BIND}"
if [[ -n "${egress}" ]]; then
  echo "  pool will see ${egress} (Cloudflare), not this box's Vast IP"
fi
echo "  SOCKS done — continuing to the miner (not waiting for input)."
