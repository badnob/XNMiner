# xnminer — hybrid low-difficulty XenBlocks miner

**`4.20.69-cuda-blkwll`** · pure C++/CUDA · no Python

This is not a stock port of the public XenBlocks clients. It is an original mining architecture I designed around how this network actually behaves: Argon2 `m=` is the cost of a hash, `/verify` is the only path that pays, and rented GPUs behind one provider NAT are not fifteen independent IPs.

The result is a miner that **force-mines at `m=100`**, **bags work until the network matches**, then **parks the GPU and lets the CPU drain `/verify`**, with **worker lanes, batch sizes, and safety caps** on both GPU and CPU so a 5090 does not cook itself or starve the submit path. When many boxes share an egress IP, **only hash submits** leave through a local WARP SOCKS. That is not a VPN. SSH stays on the machine.

---

## What I found, and what I built from it

### 1. Worker lanes — GPU and CPU — with batch sizes and caps

A Blackwell 5090 is wasted if you treat it as one queue. I split the card into **VRAM-sized CUDA lanes** (~3.5 GiB per lane at `m=100`): 8 GB→2, 16 GB→4, 24 GB→6, 32 GB→8. Batch fills **80% of VRAM**, with **20% desktop headroom**, an **emergency 95% VRAM stop**, and a **thermal batch floor of 70%** so the pack does not collapse to a trickle when memory junction climbs.

The CPU is not an afterthought. I pinned roles:

| Role | Cores (typical 8-core Vast box) | Job |
|------|----------------------------------|-----|
| CUDA host | last **2** physical | keep the GPU fed |
| Flush | first **6** | `/verify` when `m=` matches |
| Keygen | **12** threads, **off** the CUDA-host spin cores | never starve 8 lanes |

Lanes, batch, and keygen are measured on **this** box at start (`0` in `miner.ini` means auto). Caps exist so a mis-detect cannot over-commit VRAM or pin every core onto HTTP.

### 2. Park the GPU, flush on the CPU

XenBlocks does not pay hashrate. It pays **accepted `POST /verify`**. During an `m=100` window the bottleneck is sockets, not SMs. I park CUDA so the flush cores own the machine, then brute `/verify` at **512 in-flight** (one wave per second). HTTP 401/429 is a **hold**, not a reject — the hash stays in the bag. A timeout does not freeze a 90k queue.

That split — **hash on GPU, credit on CPU** — is the difference between a pretty MH/s number and blocks that actually land.

### 3. Low-difficulty force mining (hybrid `m=100`)

Live network `m=` is often 1100. Hashing at 1100 on purpose is slow. I mine **always at `m=100`**, queue every hit, and poll **only** `http://xenblocks.io/difficulty` once a second. That is what `/verify` checks. When live Net m= hits 100, the miner flushes at **512** in-flight `/verify`. When it leaves, CUDA resumes `m=100`.

**GET `/v1/leaderboard` is holdings value only** (XNM / XUNI / XBLK on the dashboard). It is never m=, never a flush clock, and never a fallback when `/difficulty` is slow. Lastblock “paper” m= is not polled either — it is sealed history and was opening flushes the pool would 401. Classic “follow the network” is still there (`force_mine_memory_cost = 0`). Hybrid is the default because it is the correct model for this protocol.

### 4. Same-IP `/verify` — a proxy, not a VPN

Vast (and any host NAT) gives many containers **one outbound IPv4**. Fifteen 512-wide flushers from that address look like one client slamming `/verify`. A full-tunnel VPN would move SSH too and lock people out of rented boxes.

I isolated **only** `POST /verify` through a **userspace WARP SOCKS** on `127.0.0.1:40000`. GPU, SSH, Woodyminer, GitHub auto-update, and the oracles stay on the box IP. No Cloudflare account. Enabled by default in `miner.ini`. Turn it off in that file and restart **the miner process**, not the machine.

---

## Network roles

| Endpoint | Role |
|----------|------|
| **GET `/difficulty`** | The only m= clock. Flush when this equals bag m=. |
| **POST `/verify`** | The only credit path. Uses live `/difficulty`. |
| **GET `/v1/leaderboard`** | Holdings value only (dashboard XNM / XUNI / XBLK). Never m=. |
| **Woodyminer** | Optional stats upload. Not XenBlocks holdings, not m=. |

---

## Defaults (what is on)

| Piece | Default |
|-------|---------|
| Hybrid force-mine `m=100` + flush on live `/difficulty` | on |
| GPU lanes / batch / keygen auto from this card | on |
| Park CUDA + 6-core CPU `/verify` (512 × 512) | on |
| WARP SOCKS for `/verify` only (`verify_warp_socks`) | **on** |
| Memory-junction hold 81 °C / cap 85 °C, batch floor 70% | on |
| VRAM pack 80% / 20% headroom / 95% emergency | on |
| Woodyminer | on |
| GitHub auto-update (bag, rebuild, restart) | on |
| Miner-set power limit | **off** (nvidia-smi / host panel owns the card) |
| XUNI hunting | **off** (queued XUNI still flush `:55–:04`) |

NVIDIA Turing or newer (RTX 20 / GTX 16+). One GPU (`device_id`). No AMD. Blackwell builds with CUDA 13 when present; 30/40-series keep the image nvcc.

`bash scripts/detect-hardware.sh` prints the plan. `./build/bin/xnminer --diagnose` dumps JSON.

---

## Vast.ai

Private repo: do **not** curl `raw.githubusercontent.com` (404). Clone with git. CUDA **devel** image (`nvcc` required).

```bash
export GH_TOKEN=ghp_xxxxxxxx
export ADDRESS=0xYourRealWalletHere
apt-get update -y && apt-get install -y git ca-certificates
git clone --depth 1 "https://x-access-token:${GH_TOKEN}@github.com/badnob/xnminer-low-dif-hybrid-blackwell.git"
cd xnminer-low-dif-hybrid-blackwell
bash vast.sh
```

Optional: `export WORKER=27605` (many people use the Vast SSH port as the name).

`bash vast.sh` **is** the dashboard. SOCKS and auto-update run in the background. That window should switch to the live miner (hashrate, temps, wallet). It is not stuck on SOCKS.

### See the dashboard (after start, after SSH drop, after an update)

SSH back in and run the same start command:

```bash
cd /workspace/xnminer-low-dif-hybrid-blackwell && bash vast.sh
```

If mining is already going, that only opens the live screen. Do not type `tmux attach` into a window that is still printing SOCKS text.

To leave the screen **without stopping the miner:** press **Ctrl+B**, then tap **D**.

Do not reboot.

Windows bag vault (Vast disks vanish): `BAG_FORWARD_URL` / `BAG_FORWARD_TOKEN` — see `HOWTO.md`. The box still flushes locally; Windows is the spare bag.

### One-liners (from the box)

```bash
# stop
pkill -9 -f '/build/bin/xnminer'; pkill -9 -f 'vast.sh'; tmux kill-session -t xnminer 2>/dev/null; tmux kill-session -t xnwrap 2>/dev/null

# start (also re-opens the dashboard if it is already mining)
cd /workspace/xnminer-low-dif-hybrid-blackwell && bash vast.sh

# pull main, rebuild, start (does not reboot the Vast instance)
bash /workspace/xnminer-low-dif-hybrid-blackwell/scripts/hard-restart.sh
```

---

## Same-IP `/verify` proxy — `miner.ini`, default on

```ini
[server]
verify_warp_socks = true
```

`true` (default): local WARP SOCKS, only hash submits. Not a VPN.  
`false`: `/verify` uses this machine’s IP.

No extra exports. No Cloudflare login. Flush width stays **512** until you change `match_drain_parallel` / `match_drain_batch` in the same file.

### Disable or enable — save the file, it restarts like auto-update

Do **not** reboot the Vast machine. Do **not** kill `vast.sh`.

1. Edit:

```bash
nano /workspace/xnminer-low-dif-hybrid-blackwell/miner.ini
```

```ini
verify_warp_socks = false
```

(or `true` to turn it back on)

2. Save: **Ctrl+O**, Enter, **Ctrl+X**.

3. Wait about 10 seconds. The miner notices the save, bags the queue, and comes back — same as auto-update. Hits stay on disk.

4. Open the dashboard again:

```bash
cd /workspace/xnminer-low-dif-hybrid-blackwell && bash vast.sh
```

If a match-flush is in progress, it finishes that window first, then restarts. If the live screen does not appear, wait a few seconds and run that command once more.

Confirm in `data/session.log`:

```text
POST /verify via proxy socks5h://127.0.0.1:40000
```

If that line is missing, submits are going out the box IP.

Flush later, if 512 through WARP 401s:

```ini
match_drain_parallel = 64
match_drain_batch = 64
```

Save the file. The miner restarts itself the same way.

---

## Local Linux

```bash
git clone https://github.com/badnob/xnminer-low-dif-hybrid-blackwell.git
cd xnminer-low-dif-hybrid-blackwell
chmod +x install-deps.sh build.sh start-miner.sh
./install-deps.sh
./start-miner.sh
```

First run asks for a `0x` wallet (or set `address =` in `miner.ini`).

```text
./build/bin/xnminer
./build/bin/xnminer --diagnose
```

`build.sh` picks `CMAKE_CUDA_ARCHITECTURES` from `nvidia-smi` (75 / 86 / 89 / 90 / 120). Override: `CMAKE_CUDA_ARCHITECTURES=120 ./build.sh`.

---

## Hardware auto-detect

| GPU family | Arch | Typical VRAM → lanes |
|------------|------|----------------------|
| Turing (20 / 16) | 75 | 8 GB → 2 |
| Ampere (30) | 86 | 8→2, 12→3, 24→6 |
| Ada (40) | 89 | 16→4, 24→6 |
| Hopper | 90 | 80 GB → 8 (cap) |
| Blackwell (50) | 120 | 16→4, 32→8 |

Empty detect → multi-arch cubin `75;86;89;90;120`. Pascal / 10-series will not build.

---

## Layout

```text
src/                 C++ host (supervisor, CUDA engine, /verify, TUI)
vendor/              CUDA Argon2id kernels (champ / work-patch)
vast.sh              Vast entry — build, open dashboard, background SOCKS + auto-update
build.sh             cmake + nvcc for this GPU
scripts/detect-hardware.sh
scripts/verify-warp-socks.sh    userspace SOCKS, no default route
scripts/hard-restart.sh         pull + rebuild, no VM reboot
miner.ini.example    0 = measure this box; verify_warp_socks = true
```
