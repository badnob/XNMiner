#!/usr/bin/env bash
# Install CUDA 13 *toolkit* (nvcc + cudart) without touching the NVIDIA driver.
#
# Desktop 16 MH/s is CUDA 13.3 SASS. Vast images ship nvcc 12.8, which compiles
# the same sm_120 kernel to ~7 MH/s. CUDA 13.x binaries run on driver >= 580
# (minor-version compatibility). Do not apt-install cuda / cuda-drivers.
set -euo pipefail

as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

nvcc_major() {
  local bin="$1"
  [[ -x "${bin}" ]] || return 1
  "${bin}" --version 2>/dev/null | sed -n 's/.*release \([0-9]\+\).*/\1/p' | head -n1
}

find_cuda13_home() {
  local d
  for d in /usr/local/cuda-13.3 /usr/local/cuda-13.2 /usr/local/cuda-13.1 /usr/local/cuda-13.0 \
           /usr/local/cuda; do
    local maj
    maj="$(nvcc_major "${d}/bin/nvcc" || true)"
    if [[ "${maj}" == "13" ]]; then
      echo "${d}"
      return 0
    fi
  done
  local which_nvcc
  which_nvcc="$(command -v nvcc 2>/dev/null || true)"
  if [[ -n "${which_nvcc}" ]]; then
    local maj
    maj="$(nvcc_major "${which_nvcc}" || true)"
    if [[ "${maj}" == "13" ]]; then
      echo "$(cd "$(dirname "${which_nvcc}")/.." && pwd)"
      return 0
    fi
  fi
  return 1
}

ubuntu_cuda_repo() {
  local ver="22.04"
  if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    ver="${VERSION_ID:-22.04}"
  fi
  case "${ver}" in
    24.04) echo ubuntu2404 ;;
    26.04) echo ubuntu2604 ;;
    20.04) echo ubuntu2004 ;;
    *) echo ubuntu2204 ;;
  esac
}

add_nvidia_cuda_repo() {
  command -v apt-get >/dev/null 2>&1 || return 1
  if [[ -f /etc/apt/sources.list.d/cuda-ubuntu2204-x86_64.list ]] ||
     [[ -f /etc/apt/sources.list.d/cuda-ubuntu2404-x86_64.list ]] ||
     [[ -f /etc/apt/sources.list.d/cuda.list ]] ||
     ls /etc/apt/sources.list.d/cuda*.list >/dev/null 2>&1; then
    return 0
  fi
  local repo
  repo="$(ubuntu_cuda_repo)"
  local deb="/tmp/cuda-keyring_1.1-1_all.deb"
  local url="https://developer.download.nvidia.com/compute/cuda/repos/${repo}/x86_64/cuda-keyring_1.1-1_all.deb"
  echo "Adding NVIDIA CUDA apt repo (${repo})..."
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL -o "${deb}" "${url}" || return 1
  else
    wget -q -O "${deb}" "${url}" || return 1
  fi
  as_root dpkg -i "${deb}" >/dev/null
  as_root apt-get update -y
}

install_cuda13_packages() {
  command -v apt-get >/dev/null 2>&1 || {
    echo "ensure-cuda13: apt-get not found; install CUDA 13 nvcc by hand." >&2
    return 1
  }
  export DEBIAN_FRONTEND=noninteractive
  # Never pull a driver stack. Hold common driver packages just in case.
  as_root apt-mark hold nvidia-driver nvidia-driver-580 nvidia-driver-570 \
    nvidia-open cuda-drivers cuda-drivers-580 2>/dev/null || true

  add_nvidia_cuda_repo || true
  as_root apt-get update -y

  local ver pkgs
  for ver in 13-3 13-2 13-1 13-0; do
    pkgs=(
      "cuda-nvcc-${ver}"
      "cuda-cudart-dev-${ver}"
      "cuda-cccl-${ver}"
    )
    echo "Trying CUDA toolkit packages: ${pkgs[*]}"
    if as_root apt-get install -y --no-install-recommends "${pkgs[@]}"; then
      echo "Installed CUDA ${ver} toolkit (compiler only)."
      return 0
    fi
    echo "Package set cuda-nvcc-${ver} not available; trying cuda-toolkit-${ver}"
    if as_root apt-get install -y --no-install-recommends "cuda-toolkit-${ver}"; then
      echo "Installed cuda-toolkit-${ver} (compiler + libs, no driver meta)."
      return 0
    fi
  done
  return 1
}

export_cuda13() {
  local home="$1"
  export CUDA_HOME="${home}"
  export CMAKE_CUDA_COMPILER="${home}/bin/nvcc"
  export CUDAToolkit_ROOT="${home}"
  export PATH="${home}/bin:${PATH}"
  export LD_LIBRARY_PATH="${home}/lib64:${LD_LIBRARY_PATH:-}"
  echo "Using ${home}/bin/nvcc ($("${home}/bin/nvcc" --version | tr '\n' ' '))"
}

ensure_cuda13() {
  local home
  if home="$(find_cuda13_home)"; then
    export_cuda13 "${home}"
    return 0
  fi
  echo "nvcc is not CUDA 13. Installing CUDA 13 toolkit (will not change the GPU driver)..."
  if ! install_cuda13_packages; then
    echo "ensure-cuda13: FAILED to install CUDA 13 nvcc. Build would stay on 12.8 (~7 MH/s)." >&2
    return 1
  fi
  if home="$(find_cuda13_home)"; then
    export_cuda13 "${home}"
    return 0
  fi
  echo "ensure-cuda13: packages installed but nvcc 13 not on disk." >&2
  return 1
}

ensure_cuda13
