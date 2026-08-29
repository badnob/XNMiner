# XNMiner CUDA

**`4.20.69-cuda-blkwll`** · pure C++/CUDA · [xenblocks.online](https://xenblocks.online)

A XenBlocks miner that follows live network `m=`, hashes Argon2id on NVIDIA Turing and newer, and credits only through `POST /verify`. Lanes are a fixed VRAM table; batch size fills leftover memory. XUNI is hunted only in the protocol window.

---

> **Warning — address ban (from 29/08/26)**  
> Enabling **block queue** (`store_blocks = true`) or **force mining** (`force_mine_memory_cost > 0`) can get your payout address **banned** if the pool rate-limits you.  
> Safe defaults are already set: follow live `/difficulty`, do not bag hashes for later. Leave them unless you accept that risk.

---

## Features

- Pure C++/CUDA host and kernels — no Python runtime
- Follows **GET `/difficulty`** as the only m= clock (number only — not a submit gate)
- Credits only via **POST `/verify`** (401/429 is a hold, not a reject); still posted when GET `/difficulty` is down, using last-good m=
- **GET `/v1/leaderboard`** is holdings on the dashboard only — never m=
- Fixed GPU lane table by VRAM: **128 GB→32, 64→16, 32→8, 16→4, 8→2, 4–6 GB→1**
- Batch size auto-fills ~80% VRAM at live m= (`batch_size = 0`)
- Memory-junction thermal hold **81 °C** / cap **85 °C**, batch floor **70%**
- CPU roles pinned: CUDA host (last 2 cores), flush (first 6), keygen off the spin cores
- XUNI mining **only** `:55–:04`; XNM / XBLK the rest of the hour
- Optional hybrid force-mine and optional queue — **off by default** (see warning)
- Match-flush `/verify` at 512 in-flight when live m= matches a bag
- Optional SOCKS/WARP for `/verify` only — GPU, SSH, and Woodyminer stay on the box IP
- Live TUI: hashrate, temps, VRAM, wallet, accepts, queue split
- Woodyminer stats upload
- Hardware probe and `--diagnose` JSON
- Turing / Ampere / Ada / Hopper / Blackwell (CUDA 13 when present)
- `miner.ini` is the source of truth — restart the miner after you edit it

---

## Network

| Endpoint | Role |
|----------|------|
| **GET `/difficulty`** | Live m= number. Mining follows this. Not a submit gate. |
| **POST `/verify`** | Only path that pays. Uses last-good m= if GET `/difficulty` is down. |
| **GET `/v1/leaderboard`** | Holdings (XNM / XUNI / XBLK) on the dashboard. Not m=. |
| **Woodyminer** | Optional stats. Not XenBlocks credit. |

---

## Defaults

| Setting | Default |
|---------|---------|
| Follow live `/difficulty` | on (`force_mine_memory_cost = 0`) |
| Store / queue blocks | **off** |
| GPU lanes from VRAM table; batch auto | on |
| XUNI hunting | on, window only |
| Memory-junction 81 / 85 °C, batch floor 70% | on |
| VRAM pack 80% / 20% headroom / 95% emergency | on |
| Woodyminer | on |
| Miner-set GPU power limit | **off** |
| WARP SOCKS for `/verify` | **off** |
| Dev fee | 1% of finds (99 yours, 100th fee) — `dev_fee = false` to turn off |

NVIDIA **Turing or newer** (RTX 20 / GTX 16+). One GPU (`device_id`). No AMD. Pascal / 10-series will not build.

---

    # Windows
    cd C:\Users\badnob\Desktop\xnminer-low-dif-hybrid-blackwell-main\xnminer-low-dif-hybrid-blackwell-main
    # Use your provided copy-paste script or start directly:
    .\start-miner.bat
    # Or as prompted by your setup:
    .\build\bin\xnminer.exe
    
    Notes:
    - `build.sh` handles architecture detection for NVIDIA cards.
    - `miner.ini` is the source of truth; restart after any changes.
    
    Alternatively, if using the standard build:
    .\build\bin\xnminer.exe --diagnose

```bash
git clone https://github.com/badnob/xnminer-low-dif-hybrid-blackwell.git
cd xnminer-low-dif-hybrid-blackwell
chmod +x install-deps.sh build.sh start-miner.sh
./install-deps.sh
./start-miner.sh
```

First run asks for a `0x` wallet, or set `address =` in `miner.ini`. Restart after any config change.

```text
./build/bin/xnminer
./build/bin/xnminer --diagnose
bash scripts/detect-hardware.sh
```

`build.sh` sets `CMAKE_CUDA_ARCHITECTURES` from `nvidia-smi` (75 / 86 / 89 / 90 / **120a**). Blackwell builds as **120a**. Override: `CMAKE_CUDA_ARCHITECTURES=120a ./build.sh`.

---

## GPU lanes

| GPU family | Arch | Typical VRAM → lanes |
|------------|------|----------------------|
| Turing (20 / 16) | 75 | 8 GB → 2 |
| Ampere (30) | 86 | 8→2, 16→4, 24→4, 32→8 |
| Ada (40) | 89 | 16→4, 24→4 |
| Hopper | 90 | 80 GB → 16 |
| Blackwell (50) | **120a** | 16→4, 32→8, 64→16, 128→32 |

Empty detect → multi-arch cubin `75;86;89;90;120a`.

---

## Config notes

Edit `miner.ini`, then restart.

```ini
force_mine_memory_cost = 0   # keep 0 unless you accept the ban risk
store_blocks = false         # keep false unless you accept the ban risk
```

Optional `/verify` proxy: `verify_proxy = socks5h://127.0.0.1:1080`, or `bash scripts/verify-warp-socks.sh` and `verify_warp_socks = true`.

---

## Layout

```text
src/                 C++ host (supervisor, CUDA engine, /verify, TUI)
vendor/              CUDA Argon2id kernels
build.sh             cmake + nvcc for this GPU
start-miner.sh       Linux start
scripts/detect-hardware.sh
scripts/verify-warp-socks.sh    optional SOCKS for /verify
miner.ini            edit, then restart
```
