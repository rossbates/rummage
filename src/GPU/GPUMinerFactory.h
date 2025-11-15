/*
 * Rummage - GPU Miner Factory
 *
 * Copyright (c) 2025 rossbates
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef GPUMINERFACTORY_H
#define GPUMINERFACTORY_H

#include "IGPUMiner.h"

#ifdef USE_CUDA
#include "cuda/CudaGPUMiner.h"
#endif

#ifdef USE_METAL
#include "metal/MetalGPUMiner.h"
#endif

/**
 * Factory function to create the appropriate GPU miner based on platform
 */
inline IGPUMiner* createGPUMiner(
    const uint8_t *gTableXCPU,
    const uint8_t *gTableYCPU,
    const char *vanityPattern,
    VanityMode mode,
    const uint8_t *startOffset,
    SearchMode searchMode = SEARCH_RANDOM,
    int bech32PatternLen = 0
) {
#ifdef USE_CUDA
    return new CudaGPUMiner(
        gTableXCPU,
        gTableYCPU,
        vanityPattern,
        mode,
        startOffset,
        searchMode,
        bech32PatternLen
    );
#elif defined(USE_METAL)
    return new MetalGPUMiner(
        gTableXCPU,
        gTableYCPU,
        vanityPattern,
        mode,
        startOffset,
        searchMode,
        bech32PatternLen
    );
#else
    #error "No GPU backend defined. Define either USE_CUDA or USE_METAL"
#endif
}

#endif // GPUMINERFACTORY_H
