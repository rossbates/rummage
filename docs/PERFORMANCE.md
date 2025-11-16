# Performance Results

This document tracks GPU performance results for Rummage with various configurations.

## GPU Performance Table

| GPU Model | Compute Capability | CUDA Version | Blocks per Grid | Threads per Block | Keys per Thread | M keys/sec | Notes |
|-----------|-------------------|--------------|-----------------|-------------------|-----------------|-------------|-------|
| RTX 3070 | 86 | 11.8 | 512 | 256 | 64 | 42 | Default config for RTX 30-series |
| H200 | 90 | 12.9 | 2816 | 256 | 64 | 170 | 20x SM count (141 SMs), ~4x performance improvement |

## Configuration Guidelines

- **NOSTR_BLOCKS_PER_GRID**: Recommended 16-32x the SM (Streaming Multiprocessor) count
- **NOSTR_THREADS_PER_BLOCK**: 256 is optimal for most kernels
- **KEYS_PER_THREAD_BATCH**: 64 is a good starting point

## Total Thread Calculations

- RTX 3070: 512 blocks × 256 threads = 131,072 concurrent CUDA threads
- H200: 2816 blocks × 256 threads = 720,896 concurrent CUDA threads

For more information on configuring Rummage for your GPU, see [BUILD.md](BUILD.md).
