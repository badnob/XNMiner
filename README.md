# 🚀 XNMiner (Hybrid-Blackwell Support)

A high-performance C++/CUDA miner designed for the **XenBlocks** network, utilizing the **Argon2id** algorithm. It is optimized for modern NVIDIA architectures, including the **Blackwell** series.

---

## ⚠️ Critical Warnings
*   **🚫 Address Ban Risk:** Enabling **block_queue** (`store_blocks = true`) or **force mining** (`force_mine_memory_cost > 0`) can result in your payout address being **banned** if the pool detects non-compliant behavior.
*   **🛡️ Safety Default:** System defaults are tuned for safe, live-network operation. Keep `store_blocks` at `false` unless you intentionally accept the risk of hardware-level rate-limits.

---

## 💎 Key Features
*   **🚀 Native Execution:** Pure C++/CUDA implementation; no Python runtime required.
*   **🔋 Adaptive Memory:** Automatic `batch_size` calculation to utilize ~80% of available VRAM.
*   **🛡️ Smart Verification:** Utilizes `POST /verify` for credits, with a fallback to the last-known good `m=` value if the primary difficulty feed is down.
*   **🌡️ Hardware Resilience:** Built-in "memory-junior" thermal logic (Hold: 81°C / Cap: 85°C) and automatic GPU lane mapping.
*   **⚡ Modern Support:** Native support for Turing, Ampere, Ada, Hopper, and **Blackwell (CUDA 120a)**.

---

## 🛠 Installation & Setup

### 💻 Windows
For users running the miner on a Windows environment, follow these steps to build from source:

#### 1. Prerequisites
Ensure you have the following installed:
*   **Visual Studio 2022** (with "Desktop development with C++" and "CUDA Toolkit" workloads)
*   **CUDA Toolkit** (12.x or higher recommended for Blackwell support)
*   **CMake** (3.18+)

#### 2. Environment Setup
Open the **Developer Command Prompt for VS 2022** (or the **x64 Native Tools Command Prompt**) to ensure the compiler environment is correctly initialized before building.

#### 3. Build the Project
Run the following commands from the project root:
```batch
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

#### 4. Execution
Once built, you can run the miner:
*   **Via Script:** `..\start-miner.bat`
*   **Directly:** `.\build\bin\xnminer.exe`

*Note: Reference `miner.ini` for all settings. Restart the miner after every change.*

### 🐧 Linux
For standard Linux installation:
```bash
git clone https://github.com/badnob/XNMiner.git
cd XNMiner
chmod +x install-deps.sh build.sh start-miner.sh
./install-deps.sh
./build.sh
./start-miner.sh
```

---

## 🌐 Ecosystem Integration
XNM1er is the primary first-party client for the **XenBlock** ecosystem. It serves as the direct feed for the **XenBlockScan** analytics suite.

### 🔗 XenBlockScan Explorer
Monitor your performance and rank on the official dashboard:
[https://explorer.xenblocks.io/leaderboard](https://explorer.xenblocks.io/leaderboard)

### 📡 Data Synchronization
The miner is designed to connect to a local gateway to report telemetry:
*   **Gateway:** `127.0.0.1:8787`
*   **Status:** Integrity is maintained via the `xenblockscan` configuration in `miner.ini`.
*   **Functions:** Automated reporting of hashrate, balance updates, and network health to the backend suite.

---

## 🚀 Advanced Functionality

### 🛣️ Hardware-Specific Routing
*   **Dynamic Lane Mapping:** 
    *   128 GB $\rightarrow$ 32 | 64 GB $\rightarrow$ 16 | 32 GB $\rightarrow$ 8 | 16 GB $\rightarrow$ 4 | 8 GB $\rightarrow$ 2 | 4-6 GB $\rightarrow$ 1
*   **Blackwell Optimization:** Specifically optimized for CUDA 120a.
*   **Automatic Scaling:** `batch_size = 0` triggers the automatic VRAM occupancy calc.

### 🌐 Networking Logic
*   **Verification Proxy:** Optionally route `verify` traffic through a SOCKS/WARP proxy while keeping other traffic on the local IP.
*   **📊 TUI Integration:** Real-time telemetry including hashrate, temperatures, and **XenBlocks** status.

---

## ⚙️ Configuration (miner.ini)
The `miner.ini` file is the source of truth. **Always restart the miner after editing.**

| Parameter | Default | Description |
| --- | --- | --- |
| `force_mine_memory_cost` | 0 | Leave at 0 to prevent address banning. |
| `store_blocks` | false | Keep false for standard operation. |
| `dev_fee` | true | Set to `false` to disable the 1% developer fee. |

---

## 📡 Network & Explorer
| Endpoint | Role | Description |
| --- | --- | --- |
| **GET** `/difficulty` | Live $m=$ | Primary heart-beat for the mining loop. |
| **POST** `/verify` | Crediting | Verifies completed work with the network. |
| **Leadership** | Dashboard | View your rank on the [XenBlocks Explorer](https://explorer.xenblocks.io/leaderboard). |

---

**Requirements:** NVIDIA Turing or newer.
**Optimizations:** CUDA 120a (Blackwell) fully supported.
