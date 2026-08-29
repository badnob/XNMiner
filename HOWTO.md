# How to run — Linux CUDA xnminer

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

The script picks `CMAKE_CUDA_ARCHITECTURES` from `nvidia-smi` compute capability **on this box** and writes `data/build_hw`. Lanes come from a **fixed VRAM table**; only batch size is measured at start (`max_lanes = 0`, `batch_size = 0`).

| Total VRAM | Lanes |
|------------|-------|
| 128 GB | 32 |
| 64 GB | 16 |
| 32 GB | 8 |
| 16 GB | 4 |
| 8 GB | 2 |
| 4–6 GB | 1 |

Blackwell installs CUDA 13 nvcc when missing (faster SASS). 30/40-series keep CUDA 12.x. Pascal / Maxwell will not build.

Blackwell is **sm_120a**. Override: `CMAKE_CUDA_ARCHITECTURES=86 ./build.sh` or `CMAKE_CUDA_ARCHITECTURES=120a ./build.sh`.

## 4. First start (new wallet)

```bash
./start-miner.sh
```

1. Enter your `0x` wallet when asked (saved to `miner.ini`).
2. Enter a miner name, or press Enter for `xnminer-xxxxxxxx`.
3. Watch the live dashboard (H/s, accepts, temp, VRAM, lanes).
4. **Ctrl+C** in the TUI stops mining and bags the queue for the next start.

There is no pre-filled address, worker, Woodyminer name, tracker id, lock, or queue.

## 5. Config

Edit `miner.ini`, then **restart xnminer** to apply:

- `[account] address` / `worker`
- `[cuda] max_lanes` / `batch_size` — **0** = auto from VRAM. `keygen_threads = 12`
- `[queue] desktop_cpu_cores = 0`
- `[efficiency]` VRAM 80%, mem-junc 85/81, miner power-limit **off**
- `[mining] force_mine_memory_cost = 0` — follow live `/difficulty`. `store_blocks = false` — do not bag hashes for later.
- `POST /verify` still runs if `GET /difficulty` is down, using last-good m=.
- `[mining] xuni_mining_enabled = true` — hunt XUNI only in the `:55–:04` window
- `[server] verify_proxy` — optional SOCKS/HTTP for `POST /verify` only

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

## Optional: WARP SOCKS for `/verify`

```bash
bash scripts/verify-warp-socks.sh
```

Then set in `miner.ini`:

```ini
verify_warp_socks = true
verify_proxy = socks5h://127.0.0.1:40000
```

Restart the miner. Confirm in `data/session.log`: `POST /verify via proxy socks5h://127.0.0.1:40000`.

## Optional: systemd

1. Put a wallet in `miner.ini` first (no interactive prompt under systemd).
2. Copy `scripts/xnminer.service` to `/etc/systemd/system/` and edit `WorkingDirectory` / `ExecStart`.
3. `sudo systemctl enable --now xnminer`
