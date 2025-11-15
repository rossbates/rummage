/*
 * Rummage - GPU Miner Abstract Interface
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

#ifndef IGPUMINER_H
#define IGPUMINER_H

#include <stdint.h>

//Maximum vanity prefix/suffix length in characters
#define MAX_VANITY_HEX_LEN 16
#define MAX_VANITY_BECH32_LEN 52

//Size definitions
#define SIZE_PRIV_KEY_NOSTR 32  // 32-byte private key
#define SIZE_PUBKEY_NOSTR 32    // 32-byte x-only public key (Schnorr)
#define SIZE_LONG 8             // Each Long is 8 bytes

//GTable configuration (same as main secp)
#define NUM_GTABLE_CHUNK 16
#define NUM_GTABLE_VALUE 65536
#define SIZE_GTABLE_POINT 32
#define COUNT_GTABLE_POINTS (NUM_GTABLE_CHUNK * NUM_GTABLE_VALUE)

// Vanity pattern matching modes
enum VanityMode {
    VANITY_HEX_PREFIX = 0,
    VANITY_HEX_SUFFIX = 1,
    VANITY_HEX_BOTH = 2,
    VANITY_BECH32_PREFIX = 3,
    VANITY_BECH32_SUFFIX = 4,
    VANITY_BECH32_BOTH = 5
};

// Search modes
enum SearchMode {
    SEARCH_RANDOM = 0,      // Random key generation (default)
    SEARCH_SEQUENTIAL = 1   // Sequential exhaustive search
};

/**
 * Abstract interface for GPU-based Nostr vanity key mining.
 * Implementations (CUDA, Metal) must implement all pure virtual methods.
 */
class IGPUMiner
{
public:
    virtual ~IGPUMiner() {}

    /**
     * Run one iteration of vanity mining
     * @param iteration The current iteration number
     */
    virtual void doIteration(uint64_t iteration) = 0;

    /**
     * Check for and print any found keys
     * @return true if any keys were found, false otherwise
     */
    virtual bool checkAndPrintResults() = 0;

    /**
     * Free GPU memory and cleanup resources
     */
    virtual void doFreeMemory() = 0;

    /**
     * Get total number of keys generated so far
     * @return Number of keys generated
     */
    virtual uint64_t getKeysGenerated() const = 0;

    /**
     * Get total number of matches found so far
     * @return Number of matches found
     */
    virtual uint64_t getMatchesFound() const = 0;

    /**
     * Save checkpoint for sequential search mode
     * @param filename Path to checkpoint file
     * @return true if successful, false otherwise
     */
    virtual bool saveCheckpoint(const char *filename) = 0;

    /**
     * Load checkpoint for sequential search mode
     * @param filename Path to checkpoint file
     * @return true if successful, false otherwise
     */
    virtual bool loadCheckpoint(const char *filename) = 0;

    /**
     * Get search progress for sequential mode
     * @return Progress from 0.0 to 1.0
     */
    virtual double getSearchProgress() const = 0;

    /**
     * Get current iteration for sequential mode
     * @return Current iteration number
     */
    virtual uint64_t getCurrentIteration() const = 0;

    /**
     * Get total iterations for sequential mode
     * @return Total number of iterations
     */
    virtual uint64_t getTotalIterations() const = 0;

    /**
     * Set bech32 verification parameters (for hex-converted patterns)
     * @param originalPattern The original bech32 pattern
     * @param originalMode The original vanity mode before conversion
     */
    virtual void setBech32Verification(const char *originalPattern, VanityMode originalMode) = 0;
};

#endif // IGPUMINER_H
