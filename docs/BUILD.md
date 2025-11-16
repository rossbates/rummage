# Build Instructions

This document explains how to build and configure Rummage for your specific GPU.

For known good configurations and performance results, see [PERFORMANCE.md](PERFORMANCE.md).

## Prerequisites

### Linux (CUDA)

**Required:**
- CUDA Toolkit (11.x or 12.x)
- GMP library (GNU Multiple Precision)
- g++ compiler

**Install dependencies (Ubuntu/Debian):**
```bash
sudo apt update
sudo apt install build-essential libgmp-dev
```

**Install CUDA:**
- Download from: https://developer.nvidia.com/cuda-downloads
- Or check if already installed: `nvcc --version`

### macOS (Metal)

**Required:**
- Xcode Command Line Tools
- GMP library via Homebrew

**Install dependencies:**
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install GMP
brew install gmp
```

## Building

### Quick Start

```bash
git clone https://github.com/rossbates/rummage.git
cd rummage
make
```

The Makefile automatically detects your platform:
- **Linux**: Builds with CUDA backend
- **macOS**: Builds with Metal backend

### First Build

The first build will:
1. Create object directories
2. Compile CPU source files
3. Compile GPU backend (CUDA or Metal)
4. Link final executable

**Expected output:**
```bash
$ make
mkdir -p obj obj/CPU obj/GPU
Building for platform: Linux  # or Darwin for macOS
GPU Backend: cuda              # or metal for macOS
...
Making rummage...
```

**Result:** `rummage` executable in current directory

### Troubleshooting Build Issues

**Build stops after compiling one CUDA file:**
- **Cause:** Missing GMP library
- **Solution:** Install libgmp-dev:
  ```bash
  sudo apt install libgmp-dev
  ```

**"fatal error: gmp.h: No such file or directory":**
- GMP headers not found
- Install: `sudo apt install libgmp-dev`

**CUDA errors about compute capability:**
- Your GPU's compute capability doesn't match Makefile setting
- See CUDA Configuration section below

**Linker errors about libgmp:**
- GMP library path is wrong for your system
- Edit Makefile line 49 to match your GMP location:
  ```bash
  # Find GMP on your system
  find /usr -name "libgmp.so*" 2>/dev/null
  ```

## CUDA Configuration (Linux)

### CUDA Compute Capability

The Makefile is configured for compute capability 86 (RTX 30-series). Update this based on your GPU:

**In Makefile, line 20:**
```make
CCAP = 86
```

**Common compute capabilities:**
- RTX 40-series (Ada): `89`
- RTX 30-series (Ampere): `86`
- RTX 20-series (Turing): `75`
- GTX 16-series (Turing): `75`
- GTX 10-series (Pascal): `61`

Find your GPU's compute capability at: https://developer.nvidia.com/cuda-gpus

## CUDA Installation Path

Update the CUDA path if installed in a non-standard location:

**In Makefile, line 21:**
```make
CUDA = /usr/local/cuda-11.8
```

Check your CUDA version:
```bash
nvcc --version
```

## GPU Thread Configuration

For optimal performance, tune these parameters in `src/GPU/GPURummage.h`:

**Lines 13-15:**
```c
#define NOSTR_BLOCKS_PER_GRID 512
#define NOSTR_THREADS_PER_BLOCK 256
#define KEYS_PER_THREAD_BATCH 64
```

**Guidelines:**
- `NOSTR_BLOCKS_PER_GRID`: Set to 16-32x your GPU's SM count
  - Find SM count: `nvidia-smi --query-gpu=count --format=csv`
  - RTX 3060: 28 SMs → use 512 blocks
  - RTX 4090: 128 SMs → try 2048+ blocks

- `NOSTR_THREADS_PER_BLOCK`: Usually optimal at 256
  - Lower values (128): Better for complex kernels
  - Higher values (512): Better for simple operations

- `KEYS_PER_THREAD_BATCH`: Keys generated per thread per iteration
  - Higher values: More throughput but more memory
  - Lower values: Less memory usage
  - Start with 64, adjust based on performance

## Rebuild After Changes

```bash
make clean
make
```

## Testing Configuration

Run a quick test to verify your build:

```bash
./rummage --npub-prefix test
```

Monitor keys/second output to validate performance. Press Ctrl+C to stop.
