/*
 * Rummage - Metal GPU Miner Implementation
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

#ifndef METALGPUMINER_H
#define METALGPUMINER_H

#include "../IGPUMiner.h"
#include <stdint.h>

// Forward declarations for Objective-C types (to keep header C++ compatible)
#ifdef __OBJC__
@class MTLDevice;
@class MTLCommandQueue;
@class MTLLibrary;
@class MTLComputePipelineState;
@class MTLBuffer;
#else
typedef void MTLDevice;
typedef void MTLCommandQueue;
typedef void MTLLibrary;
typedef void MTLComputePipelineState;
typedef void MTLBuffer;
#endif

// Metal-specific parameters
#define METAL_THREADGROUP_SIZE 256      // Threads per threadgroup (similar to CUDA block)
#define METAL_THREADGROUPS_PER_GRID 512 // Number of threadgroups (similar to CUDA grid)
#define METAL_KEYS_PER_THREAD 64        // Keys generated per thread per iteration

#define METAL_TOTAL_THREADS (METAL_THREADGROUP_SIZE * METAL_THREADGROUPS_PER_GRID)

/**
 * Metal GPU Miner Implementation
 */
class MetalGPUMiner : public IGPUMiner
{
public:
    MetalGPUMiner(
        const uint8_t *gTableXCPU,
        const uint8_t *gTableYCPU,
        const char *vanityPattern,
        VanityMode mode,
        const uint8_t *startOffset,
        SearchMode searchMode = SEARCH_RANDOM,
        int bech32PatternLen = 0
    );

    virtual ~MetalGPUMiner();

    // Implement IGPUMiner interface
    virtual void doIteration(uint64_t iteration) override;
    virtual bool checkAndPrintResults() override;
    virtual void doFreeMemory() override;
    virtual uint64_t getKeysGenerated() const override;
    virtual uint64_t getMatchesFound() const override;
    virtual bool saveCheckpoint(const char *filename) override;
    virtual bool loadCheckpoint(const char *filename) override;
    virtual double getSearchProgress() const override;
    virtual uint64_t getCurrentIteration() const override;
    virtual uint64_t getTotalIterations() const override;
    virtual void setBech32Verification(const char *originalPattern, VanityMode originalMode) override;

private:
    // Metal device and command infrastructure
    MTLDevice *device;
    MTLCommandQueue *commandQueue;
    MTLLibrary *library;
    MTLComputePipelineState *randomPipeline;
    MTLComputePipelineState *sequentialPipeline;

    // Metal buffers
    MTLBuffer *gTableXBuffer;
    MTLBuffer *gTableYBuffer;
    MTLBuffer *vanityPatternBuffer;
    MTLBuffer *startOffsetBuffer;
    MTLBuffer *resultsBuffer;
    MTLBuffer *privKeysBuffer;
    MTLBuffer *pubKeysBuffer;

    // CPU-side result buffers
    uint8_t *outputFoundCPU;
    uint8_t *outputPrivKeysCPU;
    uint8_t *outputPubKeysCPU;

    // Configuration
    uint8_t vanityLen;
    VanityMode vanityMode;
    SearchMode searchMode;
    uint8_t startOffset[32];

    // Statistics
    uint64_t keysGenerated;
    uint64_t matchesFound;
    uint64_t currentIteration;
    uint64_t totalIterations;
    uint64_t searchSpaceSize;

    // Bech32 verification
    bool needsBech32Verification;
    char originalBech32Pattern[MAX_VANITY_BECH32_LEN + 1];
    VanityMode originalBech32Mode;

    // Private helper methods
    bool initializeMetal();
    bool loadMetalLibrary();
    bool createPipelines();
    bool allocateBuffers(const uint8_t *gTableXCPU, const uint8_t *gTableYCPU);
};

#endif // METALGPUMINER_H
