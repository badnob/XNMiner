#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

echo "Installing host build deps for xnminer-cuda-linux..."

if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libcurl4-openssl-dev \
    curl \
    ca-certificates

  if ! command -v nvcc >/dev/null 2>&1; then
    if [[ -r /etc/os-release ]]; then
      # shellcheck disable=SC1091
      . /etc/os-release
    fi

    if [[ "${ID:-}" != "ubuntu" && "${ID:-}" != "debian" ]]; then
      echo "CUDA Toolkit auto-install is only wired for Ubuntu/Debian apt systems." >&2
      echo "Install nvcc manually, then rerun ./build.sh" >&2
      exit 1
    fi

    arch="x86_64"
    if command -v dpkg >/dev/null 2>&1; then
      arch="$(dpkg --print-architecture)"
      case "$arch" in
        amd64) arch="x86_64" ;;
      esac
    fi

    if [[ "${ID:-}" == "ubuntu" ]]; then
      tag="ubuntu${VERSION_ID//./}"
    else
      tag="debian${VERSION_ID//./}"
    fi

    keyring_deb="/tmp/cuda-keyring_1.1-1_all.deb"
    url="https://developer.download.nvidia.com/compute/cuda/repos/${tag}/${arch}/cuda-keyring_1.1-1_all.deb"

    echo "CUDA Toolkit not found; installing NVIDIA CUDA repository keyring..."
    curl -fsSL "$url" -o "$keyring_deb"
    sudo dpkg -i "$keyring_deb"
    sudo apt-get update
    sudo apt-get install -y cuda-toolkit
  fi
elif command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config libcurl-devel
  echo "Install CUDA Toolkit separately on this distro, then rerun ./build.sh"
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -S --needed --noconfirm base-devel cmake ninja pkgconf curl
  echo "Install CUDA Toolkit separately on this distro, then rerun ./build.sh"
else
  echo "Unknown package manager. Install: g++, cmake, ninja, pkg-config, libcurl, CUDA Toolkit." >&2
  exit 1
fi

echo
echo "Host deps OK."
echo "nvidia-smi:"
if command -v nvidia-smi >/dev/null 2>&1; then
  nvidia-smi --query-gpu=name,compute_cap,memory.total --format=csv
else
  echo "nvidia-smi not found yet — install the NVIDIA driver before mining."
fi

echo
if command -v nvcc >/dev/null 2>&1; then
  echo "nvcc:"
  nvcc --version | sed -n "1,6p"
else
  echo "nvcc still not found — install the CUDA Toolkit before building."
  exit 1
fi

if [[ -f "scripts/detect-hardware.sh" ]]; then
  bash "scripts/detect-hardware.sh" || true
fi
