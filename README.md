# xnminer-low-dif-hybrid-blackwell

Pure C++/CUDA XenBlocks miner — same **champ / work-patch** engine as the fast Windows 5090 build. Hybrid force-mine `m=100`, queue until paper/`m=` matches, then flush. Version **`4.20.69-cuda-blkwll`**. **No Python.**

At start the miner measures **nproc** and **GPU VRAM**, then splits work so a 4-core / 8 GB box and a 16-core / 32 GB 5090 both stay stable:

- CPU: bag / flush / dashboard / CUDA host slices from online cores. Flush `/verify` width is 64 / 512 / 1024 / 2048 from flush core count — never 2048 spawn-all on a small box.
- GPU: lanes from total VRAM (~3.5 GiB/lane at m=100). 8 GB→2, 16 GB→4, 24 GB→6, 32 GB→8. Batch fills 80% VRAM.
- Keygen: auto = CUDA host cores (max 16), pinned there so it cannot starve the kernel.
- Power limit is left to nvidia-smi / the host panel. Memory junction hold 81 °C, cap 85 °C.

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

That builds for the GPU in the box, writes `miner.ini`, and mines with the live TUI in tmux. Optional: `export WORKER=vast-1 DEVICE=0` before `bash vast.sh`.

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

`build.sh` picks `CMAKE_CUDA_ARCHITECTURES` from `nvidia-smi` (75 / 86 / 89 / 90 / 120). Override with `CMAKE_CUDA_ARCHITECTURES=120 ./build.sh`.

---

## What is on / off

| Feature | State |
|---------|--------|
| Hybrid force-mine `m=100` + paper-oracle brute flush (1024–2048 parallel) | on |
| Auto-update from GitHub (bag queue, rebuild, restart) | on |
| XUNI hunting | **off** — GPU stays on XNM/XBLK; queued XUNI still flush `:55–:04` |
| Woodyminer upload | on |
| 8 CUDA lanes | on |
| VRAM % cap / desktop headroom | **off** — pack the card |
| Temp limit / cooldown / thermal batch derate | **off** |
| Safety supervisor (stop GPU on temp/VRAM) | **off** |
| Keygen CPU pin (old 6-core lock) | **off** — `hardware_concurrency` threads, unpinned |

---

## Layout

```text
src/          C++ host
vendor/       CUDA Argon2id kernels
vast.sh       Vast.ai entrypoint
build.sh      cmake + nvcc
miner.ini.example
```
