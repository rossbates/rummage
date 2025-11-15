/*
 * Rummage - Metal GPU Miner Implementation (Placeholder for Phase 2)
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

/**
 * Metal GPU Miner Implementation
 *
 * This is a placeholder implementation for Phase 1.
 * Full implementation will be completed in Phase 2-5.
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
    // Metal-specific members will be added in Phase 2
    uint64_t keysGenerated;
    uint64_t matchesFound;
    uint64_t currentIteration;
    uint64_t totalIterations;
};

#endif // METALGPUMINER_H
