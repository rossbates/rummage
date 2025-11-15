# Build Configuration

This document explains how to configure Rummage for your specific GPU and CUDA installation.

## CUDA Compute Capability

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
