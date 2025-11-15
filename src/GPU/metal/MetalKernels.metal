/*
 * Rummage - Metal Compute Kernels
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

#include <metal_stdlib>
using namespace metal;

// Include our math libraries
#include "MetalMath.h"
#include "MetalMath_ModArith.h"
#include "MetalMath_EC.h"
#include "MetalMath_GTable.h"

// =========================================================================
// Random Mode Kernel (Phase 4 - to be fully implemented)
// =========================================================================

/**
 * Random mode vanity key search kernel
 *
 * Phase 3: Math library in place, basic structure ready
 * Phase 4: Will add random number generation and pattern matching
 */
kernel void nostrVanityKernel_random(
    device const uint8_t* gTableX [[buffer(0)]],
    device const uint8_t* gTableY [[buffer(1)]],
    device const uint8_t* vanityPattern [[buffer(2)]],
    device uint8_t* results [[buffer(3)]],
    device uint8_t* privKeys [[buffer(4)]],
    device uint8_t* pubKeys [[buffer(5)]],
    constant uint32_t& vanityLen [[buffer(6)]],
    constant uint32_t& vanityMode [[buffer(7)]],
    uint gid [[thread_position_in_grid]]
)
{
    // Phase 3: Test that math library compiles
    // Phase 4: Will implement full random key generation

    // For now, just mark as not found
    results[gid] = 0;

    // Test: Generate a simple public key from a known private key
    // This validates that our math library works
    if (gid == 0) {
        uint64_t test_privkey[5];
        SetInt32(test_privkey, 1); // Private key = 1

        uint64_t test_pubkey[5];
        PrivKeyToPubKey(test_pubkey, test_privkey, gTableX, gTableY);

        // Store test result (first 32 bytes of pubKeys buffer)
        Store256(pubKeys, test_pubkey);
    }
}

// =========================================================================
// Sequential Mode Kernel (Phase 4 - to be fully implemented)
// =========================================================================

/**
 * Sequential mode vanity key search kernel
 *
 * Phase 3: Math library in place, basic structure ready
 * Phase 4: Will add sequential iteration and pattern matching
 */
kernel void nostrVanityKernel_sequential(
    device const uint8_t* gTableX [[buffer(0)]],
    device const uint8_t* gTableY [[buffer(1)]],
    device const uint8_t* vanityPattern [[buffer(2)]],
    device const uint8_t* startOffset [[buffer(3)]],
    device uint8_t* results [[buffer(4)]],
    device uint8_t* privKeys [[buffer(5)]],
    device uint8_t* pubKeys [[buffer(6)]],
    constant uint64_t& iteration [[buffer(7)]],
    constant uint32_t& vanityLen [[buffer(8)]],
    constant uint32_t& vanityMode [[buffer(9)]],
    uint gid [[thread_position_in_grid]]
)
{
    // Phase 3: Test that math library compiles
    // Phase 4: Will implement full sequential search

    // For now, just mark as not found
    results[gid] = 0;

    // Test: Load start offset and compute next key
    if (gid == 0) {
        uint64_t privkey[5];
        Load256(privkey, startOffset);

        // Increment by 1
        Increment256(privkey);

        // Compute public key
        uint64_t pubkey[5];
        PrivKeyToPubKey(pubkey, privkey, gTableX, gTableY);

        // Store test result
        Store256(pubKeys, pubkey);
    }
}
