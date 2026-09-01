# XNMiner build and first-run guide

## Linux

Install the host build packages:

```bash
./install-deps.sh
```

You still need an NVIDIA driver in the Linux environment. `install-deps.sh` installs the CUDA Toolkit on apt-based Linux systems when `nvcc` is missing.

If `nvcc` is not on PATH:

```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}
```

Build from the project root:

```bash
./build.sh
```

Optional hardware preview:

```bash
bash scripts/detect-hardware.sh
```

Output:

```text
build/bin/xnminer
```

On Blackwell, the build targets `sm_120a` and expects a CUDA 13-capable toolkit.

WSL2:

- Install the NVIDIA Windows driver on the host first.
- Do not install a Linux GPU driver inside WSL.
- Install the Linux CUDA Toolkit inside the WSL distro so `nvcc --version` works.
- `install-deps.sh` installs host packages and, on apt-based Linux systems, the CUDA Toolkit if `nvcc` is missing.

## Windows

Install:

- Visual Studio 2022 Build Tools, or Visual Studio 2022
- Desktop development with C++
- CMake
- Ninja
- NVIDIA driver
- CUDA Toolkit with `nvcc`

Build from the project root:

```powershell
build.ps1
```

Or start the miner directly:

```bat
Start-Miner.bat
```

If the executable is missing, the launcher builds it first.

## First run

Run:

```bash
./start-miner.sh
```

or on Windows:

```bat
Start-Miner.bat
```

The miner detects the local GPU, VRAM, and CPU cores at startup and selects conservative defaults automatically.

## Common failures

- `nvidia-smi` not found: install the NVIDIA driver first.
- `nvcc` not found: rerun `./install-deps.sh` on an apt-based Linux system, or install the CUDA Toolkit manually, then rebuild.
- CMake cannot find CUDA: the toolkit is not installed in the current environment.
- Windows build fails: confirm Visual Studio C++ tools, CMake, Ninja, and CUDA Toolkit are installed.
