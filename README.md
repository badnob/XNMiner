# XNMiner

XNMiner is a C++/CUDA miner for the XenBlocks network. It targets NVIDIA GPUs and automatically tunes lanes, batch size, and keygen threads based on the hardware it finds at startup.

## Highlights

- Native C++/CUDA implementation
- Automatic GPU detection and runtime tuning
- Fixed VRAM-based lane selection
- Automatic batch sizing from available memory
- Cross-platform source builds on Linux and Windows
- Blackwell support with CUDA 13.x

## Supported platforms

- Linux
- Windows 10/11

## Hardware requirements

- NVIDIA GPU with CUDA support
- Recent NVIDIA driver
- CUDA Toolkit
- Sufficient system RAM and free disk space for build artifacts

Blackwell GPUs use CUDA 13.x. On older architectures, the project may build with an earlier toolkit, but the recommended setup is always the newest supported CUDA release for your device.

## Build requirements

### Linux

Install:

- g++ or clang++
- CMake
- Ninja
- pkg-config
- libcurl development headers
- NVIDIA proprietary driver
- CUDA Toolkit with nvcc

The repository includes `install-deps.sh` to install the common Linux build dependencies.

### Windows

Install:

- Visual Studio 2022 Build Tools or Visual Studio 2022
- Desktop development with C++
- CMake
- Ninja
- NVIDIA driver
- CUDA Toolkit with nvcc

Use the Visual Studio x64 native tools environment, or the provided `build.ps1` script.

## Build instructions

### Linux

From the project root:

```bash
./install-deps.sh
./build.sh
```

Quick install or update from an existing clone:

```bash
git pull --rebase && ./install-deps.sh && ./build.sh
```

Fresh clone and build in one pass:

```bash
git clone https://github.com/badnob/XNMiner.git && cd XNMiner && ./install-deps.sh && ./build.sh
```

The binary is written to:

```bash
build/bin/xnminer
```

To start the miner:

```bash
./start-miner.sh
```

### Windows

From the project root:

```bat
build.ps1
```

Or use the batch launcher:

```bat
Start-Miner.bat
```

The launcher will build the miner first if `build/bin/xnminer.exe` is missing.

## Hardware auto-detection

At startup, the miner detects the installed GPU, VRAM size, and CPU core count, then chooses conservative defaults for:

- CUDA lane count
- batch size
- keygen thread count

The goal is to get a strong first-run configuration without manual tuning. Users can still adjust `miner.ini` later if they want to override the defaults.

You can preview the detected configuration with:

```bash
bash scripts/detect-hardware.sh
```

or:

```bash
./build/bin/xnminer --diagnose
```

## Configuration

`miner.ini` is the source of truth for runtime settings. Restart the miner after editing it.

Important settings include:

- `force_mine_memory_cost = 0` — keep this at zero for normal use
- `store_blocks = false` — recommended for standard operation
- `max_lanes = 0` — auto-select lanes from VRAM
- `batch_size = 0` — auto-select batch size from available VRAM

## Notes on safety and performance

- The project is designed to tune itself from the local hardware profile.
- Build on the machine that will mine whenever possible.
- If you update drivers or the CUDA Toolkit, rebuild the project afterwards.
- If the miner starts with unexpected performance, confirm that the correct binary was built for the local GPU and that `miner.ini` does not contain stale overrides.

## Troubleshooting

- `nvidia-smi` must work before you build or run the miner.
- `nvcc --version` must report the installed CUDA Toolkit.
- If Windows fails to build, confirm that Visual Studio C++ tools, CMake, Ninja, and the CUDA Toolkit are installed.
- If Linux fails to build, confirm that the CUDA Toolkit and the development packages listed above are present.

## Optional networking tools

The repository also includes optional scripts for users who need them:

- `scripts/verify-warp-socks.sh` for a local SOCKS proxy used by `/verify`
- `scripts/xnminer.service` for systemd-based service installs on Linux

## License

Refer to the repository for licensing details.
