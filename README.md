# xnminer-low-dif-hybrid-blackwell

Pure C++/CUDA XenBlocks miner — same **champ / work-patch** engine as the fast Windows 5090 build. Hybrid force-mine `m=100`, queue until paper/`m=` matches, then flush. Version **`4.20.69-cuda-blkwll`**. **No Python.**

**New hardware is auto-detected.** `build.sh` reads `nvidia-smi` (GPU name, SM arch, VRAM) and `nproc` (CPU cores). The cubin is compiled for **that** GPU. At start the miner sizes lanes, batch, keygen, and `/verify` width from this box:

- **Build arch:** Turing 75 / Ampere 86 / Ada 89 / Hopper 90 / Blackwell 120. Empty detect → multi-arch cubin (`75;86;89;90;120`). Override with `CMAKE_CUDA_ARCHITECTURES=86 ./build.sh`.
- **CUDA toolkit:** Blackwell prefers CUDA 13 (faster SASS). 30/40-series keep the image’s nvcc (12.x is fine — CUDA 13 is not installed on those boxes).
- **CPU:** last 2 physical cores = CUDA host while mining. At m=100 CUDA **parks**; first 6 cores run **512** `/verify` workers, one wave per second. Dashboard has no reserved cores (Uptime still on the TUI).
- **GPU lanes:** from total VRAM (~3.5 GiB/lane at m=100). 8 GB→2, 16 GB→4, 24 GB→6, 32 GB→8. Batch fills 80% VRAM.
- **Keygen:** 12 threads, pinned **off** the CUDA-host spin cores so 8 lanes do not starve the GPU.
- **Power:** left to nvidia-smi / the host panel. Memory junction hold 81 °C, cap 85 °C.

`bash scripts/detect-hardware.sh` prints the plan without mining. `./build/bin/xnminer --diagnose` dumps it as JSON.

Needs an **NVIDIA Turing-or-newer** GPU (RTX 20 / GTX 16 and up). No AMD, no CPU mining, one GPU (`device_id`).

GPU always keeps hashing during flush. Mixed lastblock windows keep mining while the matching bag goes out. 401/timeout does not freeze the bag.

---

## One-liners

Run these from the box. Update needs `GH_TOKEN` in the environment (or in the live `vast.sh` process).

**Kill**
```bash
pkill -9 -f '/build/bin/xnminer'; pkill -9 -f 'bash vast.sh'
```

**Update** (kill, pull `origin/main`, rebuild, start)
```bash
bash /workspace/xnminer-low-dif-hybrid-blackwell/scripts/hard-restart.sh
```

**Start**
```bash
cd /workspace/xnminer-low-dif-hybrid-blackwell && bash vast.sh
```

---

## Vast.ai (private repo)

`raw.githubusercontent.com` returns **404** on private repos. Clone with git instead.

Replace `ghp_…` with a real PAT (`repo` scope) and `0x…` with your wallet — not the words `YOUR_PAT` / `YOUR_WALLET`.

```bash
export GH_TOKEN=ghp_xxxxxxxx
export ADDRESS=0xYourRealWalletHere
apt-get update -y && apt-get install -y git ca-certificates
git clone --depth 1 "https://x-access-token:${GH_TOKEN}@github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"
cd xnminer-low-dif-hybrid-blackwell
bash vast.sh
```

That detects the GPU in the box (SM + VRAM + CPU), builds the matching cubin, writes `miner.ini` with auto lanes/batch/CPU (`0` = measure at start), and mines with the live TUI in tmux. Optional: `export WORKER=vast-1 DEVICE=0` before `bash vast.sh`.

Watch it over SSH (same Campbell dashboard theme as the Windows miner — no tmux status bar):
```bash
tmux attach -t xnminer
```
Detach without stopping: **Ctrl+B** then **D**. Ctrl+C in the TUI stops the miner and bags the queue.

To back up the local bag onto your Windows miner (so a wiped Vast box does not lose queued hits):

```bash
export BAG_FORWARD_URL=http://YOUR.HOME.IP:18787/bag
export BAG_FORWARD_TOKEN=the_token_from_windows_miner.ini
```

Windows miner must have `bag_ingest_enabled = true` and that port forwarded (or Tailscale). Vast still flushes locally when `m=` matches; Windows is the spare bag.

Use a **CUDA devel** template (has `nvcc`), not a runtime-only image.

---

## Local Linux

```bash
git clone https://github.com/badnob/xnminer-low-dif-hybrid-blackwell.git
cd xnminer-low-dif-hybrid-blackwell
chmod +x install-deps.sh build.sh start-miner.sh
./install-deps.sh
./start-miner.sh
```

First run prompts for a `0x` wallet (or set `address =` in `miner.ini`).

```text
./build/bin/xnminer
./build/bin/xnminer
./build/bin/xnminer --diagnose
```

`build.sh` picks `CMAKE_CUDA_ARCHITECTURES` from `nvidia-smi` (75 / 86 / 89 / 90 / 120) and sizes lanes/batch from VRAM. Override with `CMAKE_CUDA_ARCHITECTURES=120 ./build.sh`. Preview: `bash scripts/detect-hardware.sh`.

---

## What is on / off

| Feature | State |
|---------|--------|
| Hybrid force-mine `m=100` + paper-oracle brute flush | on (`/verify` width auto from CPU) |
| Auto GPU arch / VRAM lanes / CPU split | **on** — `0` in `miner.ini` means measure this box |
| Auto-update from GitHub (bag queue, rebuild, restart) | on |
| XUNI hunting | **off** — GPU stays on XNM/XBLK; queued XUNI still flush `:55–:04` |
| Woodyminer upload | on |
| CUDA lanes | **auto** from VRAM (not stuck at 8) |
| VRAM pack | **on** — 80% target, 20% desktop headroom |
| Memory-junction thermal hunt | **on** — hold 81 °C / cap 85 °C, batch floor 70% |
| Miner power-limit | **off** — nvidia-smi / host panel owns the card |
| Keygen CPU pin | **on** — auto thread count, pinned to CUDA-host cores |

---

## Layout

```text
src/          C++ host
vendor/       CUDA Argon2id kernels
vast.sh       Vast.ai entrypoint
build.sh      cmake + nvcc (arch from this GPU)
scripts/detect-hardware.sh
miner.ini.example          # 0 = auto from this box
```
