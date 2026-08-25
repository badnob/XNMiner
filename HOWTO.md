# How to run — Linux CUDA xnminer

## Vast.ai

```bash
export GH_TOKEN=ghp_xxxxxxxx
export ADDRESS=0xYourRealWalletHere
apt-get update -y && apt-get install -y git ca-certificates
git clone --depth 1 "https://x-access-token:${GH_TOKEN}@github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"
cd xnminer-low-dif-hybrid-blackwell
bash vast.sh
```

Use a CUDA **devel** image so `nvcc` exists.

## 1. Host packages

```bash
./install-deps.sh
```

Needs: **g++**, **CMake**, **Ninja**, **pkg-config**, **libcurl**.

## 2. NVIDIA stack

1. Install the NVIDIA proprietary driver until `nvidia-smi` lists your GPU.
2. Install the CUDA Toolkit so `nvcc --version` works.
3. If `nvcc` is missing from `PATH`:

```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}
```

## 3. Build

```bash
bash scripts/detect-hardware.sh   # optional preview: GPU, SM, VRAM, CPU, lanes, batch
./build.sh
```

Output: `build/bin/xnminer`

The script picks `CMAKE_CUDA_ARCHITECTURES` from `nvidia-smi` compute capability **on this box** and writes `data/build_hw`. Lanes and batch are **not** baked in — the running miner measures VRAM and CPU again at start (`max_lanes = 0`, `batch_size = 0`, `keygen_threads = 0`).

| GPU family | Typical arch | Typical VRAM → lanes |
|------------|--------------|----------------------|
| Turing (20-series) | 75 | 8 GB → 2 |
| Ampere (30-series) | 86 | 8 GB → 2, 12 GB → 3, 24 GB → 6 |
| Ada (40-series) | 89 | 16 GB → 4, 24 GB → 6 |
| Hopper | 90 | 80 GB → 8 (cap) |
| Blackwell (50-series) | 120 | 32 GB → 8, 16 GB → 4 |

Blackwell installs CUDA 13 nvcc when missing (faster SASS). 30/40-series keep CUDA 12.x. Pascal / Maxwell will not build.

Override: `CMAKE_CUDA_ARCHITECTURES=86 ./build.sh`

## 4. First start (new wallet)

```bash
./start-miner.sh
```

1. Enter your `0x` wallet when asked (saved to `miner.ini`).
2. Enter a miner name, or press Enter for `xnminer-xxxxxxxx`.
3. Watch the live dashboard (H/s, accepts, temp, VRAM, lanes). On Vast the miner runs in tmux:
   `tmux attach -t xnminer`
   Detach without stopping: **Ctrl+B** then **D**.
4. **Ctrl+C** in the TUI stops mining and bags the queue for the next start.

There is no pre-filled address, worker, Woodyminer name, tracker id, lock, or queue.

## 5. Config

Edit `miner.ini` (created from `miner.ini.example`):

- `[account] address` / `worker`
- `[cuda] max_lanes` / `keygen_threads` / `batch_size` — leave at **0** to auto-size from this box's VRAM and CPU count
- `[queue] desktop_cpu_cores` / `flush_cpu_cores` / `bag_sort_cpu_cores` / `dashboard_cpu_cores` — **0 = auto from nproc**. Dedicated Vast boxes keep `desktop_cpu_cores = 0`
- `[efficiency]` VRAM 80%, mem-junc 85/81, miner power-limit **off**
- `[mining] match_drain_parallel = 0` auto-caps `/verify` workers from flush cores (256 / 1024 / 2048). Dummy `/verify` held 1024 in-flight with 0 timeouts; ~23k–100k POSTs per 30s at ~300ms.
- `[queue] bag_forward_url` / `bag_forward_token` — copy queued hits to the Windows home vault

## Checks

| Check | Where |
|-------|--------|
| Detected GPU / CPU / lanes | `bash scripts/detect-hardware.sh` or `./build/bin/xnminer --diagnose` |
| Hashrate | Dashboard H/s |
| Accepts | Accepted counters |
| Logs | `data/session.log` |
| What this binary was built for | `data/build_hw` |
| Remote | woodyminer.com (on by default) |

## Common issues

| Problem | Fix |
|---------|-----|
| Build fails | `./install-deps.sh`; install CUDA Toolkit |
| `nvcc not found` | Add `/usr/local/cuda/bin` to `PATH` |
| CUDA start failed | Update NVIDIA driver; `bash scripts/detect-hardware.sh` then rebuild |
| Wrong SM cubin | Delete `build/` and run `./build.sh` on the box that will mine |
| Pascal / 10-series | Not supported (needs Turing sm_75 or newer) |
| Power limit fails | Run as root, or skip `gpu_power_boost_enabled` |
| Another instance | Close the other miner or delete `data/miner.lock` |
| Woodyminer HTTPS fails | Confirm `libcurl` is the OpenSSL build (`libcurl4-openssl-dev`) |

## Auto-update

`vast.sh` watches GitHub. When `main` moves, it SIGTERM the miner (that **bags the queue to disk**), `git pull`s, rebuilds, and starts again. The miner also self-checks every 5 minutes and exits `75` for the same path. No manual stop. Do not interrupt a match-flush window — the checker waits.

When paper or `/difficulty` matches bag `m=`, CPU brute-flushes `/verify` (256 / 1024 / 2048 in-flight from flush cores; 2048 on 25k+ bags when the box has 5+ flush cores). A 401/timeout no longer pauses the whole bag. Dummy `/verify` held 1024 in-flight with 0 timeouts (~23k–100k POSTs per 30s at ~300ms).

Needs `GH_TOKEN` (already required to clone the private repo).

## Home vault (Windows backup bag)

Vast disks vanish when the instance dies. The miner can copy every queued hit to your Windows miner and still flush locally when `m=` matches.

On the Windows PC: rebuild xnminer-cuda, enable `bag_ingest_enabled`, port-forward **18787**, copy `bag_ingest_token` from `miner.ini`.

On Vast, before `bash vast.sh`:

```bash
export BAG_FORWARD_URL=http://YOUR.HOME.IP:18787/bag
export BAG_FORWARD_TOKEN=the_token_from_windows_miner.ini
```

## Optional: systemd

1. Put a wallet in `miner.ini` first (no interactive prompt under systemd).
2. Copy `scripts/xnminer.service` to `/etc/systemd/system/` and edit `WorkingDirectory` / `ExecStart`.
3. `sudo systemctl enable --now xnminer`
