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

`bash vast.sh` opens the live miner dashboard in that same window. SOCKS setup runs in the background and is not a prompt. After an SSH drop, SSH back in and run `bash vast.sh` again to see the dashboard. Leave without stopping: **Ctrl+B** then **D**.

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
3. Watch the live dashboard (H/s, accepts, temp, VRAM, lanes). On Vast, `bash vast.sh` opens that dashboard in the same window. Detach without stopping: **Ctrl+B** then **D**. Run `bash vast.sh` again after reconnect.
4. **Ctrl+C** in the TUI stops mining and bags the queue for the next start.

There is no pre-filled address, worker, Woodyminer name, tracker id, lock, or queue.

## 5. Config

Edit `miner.ini` (created from `miner.ini.example`):

- `[account] address` / `worker`
- `[cuda] max_lanes` / `batch_size` — **0** = auto from VRAM. `keygen_threads = 12` (desktop; pinned off CUDA-host cores)
- `[queue] desktop_cpu_cores = 0`, bag/flush/dashboard **2** on an 8-core Vast box. CUDA host is the last 2 physical cores.
- `[efficiency]` VRAM 80%, mem-junc 85/81, miner power-limit **off**
- `[mining] match_drain_parallel = 512` and `match_drain_batch = 512` — one 512-wide `/verify` wave per second. CUDA parks at m=100 so 6 CPU cores own flush.
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
| Window sits on SOCKS / looks stuck | That is the background proxy, not the miner. Run `bash vast.sh` again in that clone folder. Do not type `tmux attach` into the SOCKS text. |

## Auto-update

`vast.sh` watches GitHub. When `main` moves, it SIGTERM the miner (that **bags the queue to disk**), `git pull`s, rebuilds, and starts again. The miner also self-checks every 5 minutes and exits `75` for the same path. No manual stop. Do not interrupt a match-flush window — the checker waits.

When live `/difficulty` matches bag `m=`, CPU brute-flushes `/verify`. Lastblock paper is not used. A 401/timeout no longer pauses the whole bag.

Needs `GH_TOKEN` (already required to clone the private repo).

## Local WARP SOCKS (free, not a VPN, SSH stays up)

This is the “only `/verify` leaves through another IP” path. It does **not** change the box default route. SSH, CUDA, Woodyminer, GitHub stay on Vast.

**On by default** in `miner.ini`:

```ini
verify_warp_socks = true
```

`bash vast.sh` starts a userspace Cloudflare WARP SOCKS at `127.0.0.1:40000` and points only `POST /verify` at it. No extra export, no Cloudflare account. Flush stays **512**. SOCKS logs stay in the background (`data/wrapper.log`); this window should become the miner dashboard.

To disable without rebooting the Vast machine: set `verify_warp_socks = false` in `miner.ini` and save. The miner sees the file change, bags the queue, and `vast.sh` restarts it — the same path as auto-update. Wait about 10 seconds, then open the dashboard:

```bash
cd /workspace/xnminer-low-dif-hybrid-blackwell && bash vast.sh
```

Leave without stopping: **Ctrl+B**, then **D**.

Check `data/session.log` for `POST /verify via proxy socks5h://127.0.0.1:40000`. If WARP cannot start, the miner still runs and `/verify` uses the box IP.

```bash
curl -4 -fsS https://ifconfig.me                  # Vast IP — SSH uses this
curl -4 -fsS -x socks5h://127.0.0.1:40000 https://ifconfig.me   # Cloudflare IP — pool sees this
```

All of your miners may still share a handful of Cloudflare addresses. If 512 through WARP 401s, then drop the flush width. Do not use random public proxy lists: `/verify` is HTTP and includes your wallet.

## Per-box `/verify` proxy (shared Vast IP)

Vast instances on one host share an outbound IP. Fifteen boxes × 512 `/verify` from that IP is how the pool 401s you. A single free VPN for the fleet is the same problem (one shared egress). Do **not** wrap the whole container in WARP/WireGuard — SSH and the GPU stay on the Vast IP; only POST `/verify` should leave via a unique SOCKS.

**1. One cheap VPS per unique IP** (Ubuntu/Debian). Copy `scripts/verify-proxy-exit.sh` from this tree (private repo — do not curl `raw.githubusercontent.com`). As root on the VPS:

```bash
bash verify-proxy-exit.sh
```

It prints a `VERIFY_PROXY=socks5h://…` line. Save it. Repeat on each VPS so every public IP is different.

**2. On that Vast box**, after this build is running (git pull / auto-update):

```bash
export VERIFY_PROXY=socks5h://USER:PASS@VPS.IP:1080
curl -4 -fsS --max-time 15 -x "$VERIFY_PROXY" https://ifconfig.me
# must print the VPS IP, not the Vast IP
cd /workspace/xnminer-low-dif-hybrid-blackwell && bash vast.sh
```

`socks5h` resolves `xenblocks.io` on the exit. Woodyminer, GitHub auto-update, and `/difficulty` stay on the box IP. Tony.x1 can stay direct. Flush width is whatever `miner.ini` says (default 512).

**3. Confirm** in `data/session.log`:

```text
POST /verify via proxy socks5h://***@x.x.x.x:1080 (oracles/Woodyminer stay on box IP)
```

Fewer VPS than GPUs: put 2–3 miners on one exit, keep width 64. Never point all 15 at one SOCKS.

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
