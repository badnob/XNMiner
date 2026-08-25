#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT}"

BIN="${ROOT}/build/bin/xnminer"
if [[ ! -x "${BIN}" ]]; then
  echo "Building pure CUDA miner..."
  "${ROOT}/build.sh"
fi

if [[ ! -f "${ROOT}/miner.ini" ]]; then
  cp "${ROOT}/miner.ini.example" "${ROOT}/miner.ini"
  echo "Created miner.ini from miner.ini.example (empty wallet — first run will prompt)."
fi

exec "${BIN}" "$@"
