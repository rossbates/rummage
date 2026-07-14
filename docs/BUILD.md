# Build Instructions

This document explains how to build and configure Rummage for your specific GPU.

For known good configurations and performance results, see [PERFORMANCE.md](PERFORMANCE.md).

## Prerequisites

### Linux (CUDA)

**Required:**
- CUDA Toolkit (11.x, 12.x, or 13.x)
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
- RTX 50-series (Blackwell): `120`
- RTX 40-series (Ada): `89`
- RTX 30-series (Ampere): `86`
- RTX 20-series (Turing): `75`
- GTX 16-series (Turing): `75`
- GTX 10-series (Pascal): `61`

Find your GPU's compute capability at: https://developer.nvidia.com/cuda-gpus

`CCAP` can also be overridden without editing the Makefile, e.g. `make CCAP=120`.

## CUDA Installation Path

The Makefile defaults to `CUDA = /usr/local/cuda`, which is normally a symlink to
whichever version you last installed. If you have multiple CUDA versions installed
and need a specific one, override it on the command line instead of editing the
Makefile:

```bash
make CUDA=/usr/local/cuda-11.8
```

Check your CUDA version:
```bash
nvcc --version
```

**Newer glibc (2.39+) build error:**
```
error: exception specification is incompatible with that of previous function "rsqrt"
```
This happens because recent glibc versions declare `rsqrt`/`rsqrtf` (C23 extensions)
with a different exception specification than CUDA's `math_functions.h`. The Makefile
already compiles `GPURummage.cu` with `-Xcompiler -U_GNU_SOURCE` to avoid pulling in
those glibc declarations, which resolves the conflict on CUDA 11.8 through 13.x.

## GPU Performance Tuning

For optimal performance, tune these parameters in the Makefile:

**In Makefile, lines 29-31:**
```make
NOSTR_BLOCKS_PER_GRID   = 512
NOSTR_THREADS_PER_BLOCK = 256
KEYS_PER_THREAD_BATCH   = 64
```

**Guidelines:**
- `NOSTR_BLOCKS_PER_GRID`: Set to 16-32x your GPU's SM count
  - Find SM count: `nvidia-smi --query-gpu=compute_cap --format=csv`
  - RTX 3060 (28 SMs) → use 512 blocks (18x)
  - RTX 3070 (46 SMs) → use 512-1024 blocks
  - RTX 4090 (128 SMs) → use 2048-4096 blocks (16-32x)
  - Tesla P100 (56 SMs) → use 1120 blocks (20x)
  - H200 (141 SMs) → use 2816 blocks (20x)

- `NOSTR_THREADS_PER_BLOCK`: Usually optimal at 256
  - This is the number of threads per block
  - 256 works well for most GPUs
  - Don't change unless you have specific performance data

- `KEYS_PER_THREAD_BATCH`: Keys generated per thread per iteration
  - Higher values: More throughput but more memory
  - Lower values: Less memory usage
  - 64 is a good default for all GPUs

**Total thread count:** `NOSTR_BLOCKS_PER_GRID × NOSTR_THREADS_PER_BLOCK`
- RTX 3070 (512 × 256): 131,072 threads
- H200 (2816 × 256): 720,896 threads

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
