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
#include "MetalMath_26bit.h"  // New 26-bit limb representation

// =========================================================================
// Bech32 Constants and Functions
// =========================================================================

constant char BECH32_CHARSET[33] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
constant uint32_t BECH32_GEN[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};

// Bech32 polymod for checksum calculation
inline uint32_t bech32_polymod(thread const uint8_t *values, int len) {
    uint32_t chk = 1;

    for (int i = 0; i < len; i++) {
        uint8_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ values[i];
        for (int j = 0; j < 5; j++) {
            if ((top >> j) & 1) {
                chk ^= BECH32_GEN[j];
            }
        }
    }
    return chk;
}

// Convert 8-bit to 5-bit for bech32
inline void convert_bits_8to5(thread uint8_t *out, thread int *outlen, thread const uint8_t *in, int inlen) {
    uint32_t acc = 0;
    int bits = 0;
    int maxv = 31; // (1 << 5) - 1
    int max_acc = (1 << (8 + 5 - 1)) - 1;
    *outlen = 0;

    for (int i = 0; i < inlen; i++) {
        acc = ((acc << 8) | in[i]) & max_acc;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[(*outlen)++] = (acc >> bits) & maxv;
        }
    }

    if (bits > 0) {
        out[(*outlen)++] = (acc << (5 - bits)) & maxv;
    }
}

// Encode pubkey to npub (without "npub1" prefix)
inline void encode_npub(thread const uint8_t *pubkey, thread char *npub_out) {
    uint8_t data5[52];
    int data5_len;
    convert_bits_8to5(data5, &data5_len, pubkey, 32);

    // Create values array: HRP expansion + data + 6 zeros for checksum
    uint8_t values[63];
    values[0] = 3; values[1] = 3; values[2] = 3; values[3] = 3; values[4] = 16; // "npub"

    for (int i = 0; i < data5_len; i++) {
        values[5 + i] = data5[i];
    }
    for (int i = 0; i < 6; i++) {
        values[5 + data5_len + i] = 0;
    }

    // Calculate checksum
    uint32_t polymod = bech32_polymod(values, 5 + data5_len + 6) ^ 1;

    // Encode data
    for (int i = 0; i < data5_len; i++) {
        npub_out[i] = BECH32_CHARSET[data5[i]];
    }

    // Append checksum
    for (int i = 0; i < 6; i++) {
        npub_out[data5_len + i] = BECH32_CHARSET[(polymod >> (5 * (5 - i))) & 31];
    }

    npub_out[data5_len + 6] = '\0';
}

// =========================================================================
// Pattern Matching Functions
// =========================================================================

// Hex charset constant
constant char HEX_CHARS[17] = "0123456789abcdef";

// Convert byte to hex characters
inline void byteToHex(uint8_t byte, thread char *hex) {
    hex[0] = HEX_CHARS[(byte >> 4) & 0xF];
    hex[1] = HEX_CHARS[byte & 0xF];
}

// Check hex pattern match
inline bool matchesHexPattern(thread const uint8_t *pubkey, device const uint8_t *pattern, uint8_t patternLen, bool isPrefix) {
    char hex[2];

    if (isPrefix) {
        for (uint8_t i = 0; i < patternLen; i++) {
            byteToHex(pubkey[i / 2], hex);
            if (i % 2 == 0) {
                if (hex[0] != pattern[i]) return false;
            } else {
                if (hex[1] != pattern[i]) return false;
            }
        }
    } else {
        int pubkeyByteLen = 32;
        int startByte = pubkeyByteLen - ((patternLen + 1) / 2);
        int startChar = (patternLen % 2 == 1) ? 1 : 0;

        for (uint8_t i = 0; i < patternLen; i++) {
            int byteIdx = startByte + (i + startChar) / 2;
            byteToHex(pubkey[byteIdx], hex);
            if ((i + startChar) % 2 == 0) {
                if (hex[0] != pattern[i]) return false;
            } else {
                if (hex[1] != pattern[i]) return false;
            }
        }
    }

    return true;
}

// Check bech32 pattern match
inline bool matchesBech32Pattern(thread const char *npub, device const uint8_t *pattern, uint8_t patternLen, bool isPrefix) {
    if (isPrefix) {
        for (uint8_t i = 0; i < patternLen; i++) {
            if (npub[i] != pattern[i]) return false;
        }
    } else {
        int data_len = 52;
        int start_pos = data_len - patternLen;
        for (uint8_t i = 0; i < patternLen; i++) {
            if (npub[start_pos + i] != pattern[i]) return false;
        }
    }
    return true;
}

// =========================================================================
// Random Number Generation (Simple LCG for Metal)
// =========================================================================

// Simple linear congruential generator
inline uint32_t lcg_random(thread uint64_t *seed) {
    *seed = (*seed * 6364136223846793005ULL + 1442695040888963407ULL);
    return (uint32_t)(*seed >> 32);
}

// Initialize seed based on thread ID and global iteration
inline uint64_t init_seed(uint32_t gid, uint64_t iteration) {
    // Combine thread ID and iteration to create unique seed per thread per iteration
    return ((uint64_t)gid << 32) | (iteration & 0xFFFFFFFF);
}

// =========================================================================
// Random Mode Kernel
// =========================================================================

#define KEYS_PER_THREAD 64

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
    // MINIMAL TEST: Just generate a private key and copy it to pubkey
    // This tests if basic kernel execution works without EC math

    // Initialize RNG seed for this thread
    uint64_t seed = init_seed(gid, 0);

    // Generate ONE random key (not a batch)
    uint8_t privKey[32];
    for (int i = 0; i < 8; i++) {
        uint32_t rand = lcg_random(&seed);
        privKey[i*4 + 0] = (rand >> 24) & 0xFF;
        privKey[i*4 + 1] = (rand >> 16) & 0xFF;
        privKey[i*4 + 2] = (rand >> 8) & 0xFF;
        privKey[i*4 + 3] = rand & 0xFF;
    }

    // Ensure not zero
    bool isZero = true;
    for (int i = 0; i < 32; i++) {
        if (privKey[i] != 0) {
            isZero = false;
            break;
        }
    }
    if (isZero) privKey[31] = 1;

    // Compute public key: privkey * G
    ECPoint_Jac_26 pubkey_jac;
    gtable_mult_g_jac_26(&pubkey_jac, privKey, gTableX, gTableY);

    // Convert to affine coordinates
    ECPoint_Aff_26 pubkey_aff;
    ec_jac_to_affine_26(&pubkey_aff, &pubkey_jac);

    // Store affine x coordinate as public key
    uint8_t pubKey[32];
    store_26(pubKey, &pubkey_aff.x);

    // Check pattern match
    bool matched = false;

    if (vanityMode == 0) {
        // Hex prefix
        matched = matchesHexPattern(pubKey, vanityPattern, vanityLen, true);
    } else if (vanityMode == 1) {
        // Hex suffix
        matched = matchesHexPattern(pubKey, vanityPattern, vanityLen, false);
    } else if (vanityMode == 2) {
        // Hex prefix + suffix
        uint8_t halfLen = vanityLen / 2;
        matched = matchesHexPattern(pubKey, vanityPattern, halfLen, true) &&
                  matchesHexPattern(pubKey, vanityPattern + halfLen, vanityLen - halfLen, false);
    } else if (vanityMode == 3 || vanityMode == 4 || vanityMode == 5) {
        // Bech32 modes - need to encode first
        char npub[64];
        encode_npub(pubKey, npub);

        if (vanityMode == 3) {
            matched = matchesBech32Pattern(npub, vanityPattern, vanityLen, true);
        } else if (vanityMode == 4) {
            matched = matchesBech32Pattern(npub, vanityPattern, vanityLen, false);
        } else if (vanityMode == 5) {
            uint8_t halfLen = vanityLen / 2;
            matched = matchesBech32Pattern(npub, vanityPattern, halfLen, true) &&
                      matchesBech32Pattern(npub, vanityPattern + halfLen, vanityLen - halfLen, false);
        }
    }

    // If matched, store result
    if (matched) {
        results[gid] = 1;
        for (int i = 0; i < 32; i++) {
            pubKeys[gid * 32 + i] = pubKey[i];
            privKeys[gid * 32 + i] = privKey[i];
        }
    } else {
        results[gid] = 0;
    }
}

// =========================================================================
// Sequential Mode Kernel
// =========================================================================

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
    uint gid [[thread_position_in_grid]],
    uint total_threads [[threads_per_grid]]
)
{
    // Process multiple keys per thread for efficiency
    for (int batch = 0; batch < KEYS_PER_THREAD; batch++) {
        // Calculate sequential key index for this thread
        uint64_t keyIndex = iteration * total_threads * KEYS_PER_THREAD + gid * KEYS_PER_THREAD + batch;

        // Load start offset
        uint8_t privKey[32];
        for (int i = 0; i < 32; i++) {
            privKey[i] = startOffset[i];
        }

        // Add keyIndex to offset (256-bit addition)
        uint64_t carry = keyIndex;
        for (int i = 31; i >= 0 && carry > 0; i--) {
            uint64_t sum = privKey[i] + (carry & 0xFF);
            privKey[i] = sum & 0xFF;
            carry = (carry >> 8) + (sum >> 8);
        }

        // Ensure not zero
        bool isZero = true;
        for (int i = 0; i < 32; i++) {
            if (privKey[i] != 0) {
                isZero = false;
                break;
            }
        }
        if (isZero) continue;

        // Compute public key: privkey * G
        ECPoint_Jac_26 pubkey_jac;
        gtable_mult_g_jac_26(&pubkey_jac, privKey, gTableX, gTableY);

        // Convert to affine coordinates
        ECPoint_Aff_26 pubkey_aff;
        ec_jac_to_affine_26(&pubkey_aff, &pubkey_jac);

        // Store affine x coordinate as public key
        uint8_t pubKey[32];
        store_26(pubKey, &pubkey_aff.x);

        // Check pattern match (same logic as random mode)
        bool matched = false;

        if (vanityMode == 0) {
            matched = matchesHexPattern(pubKey, vanityPattern, vanityLen, true);
        } else if (vanityMode == 1) {
            matched = matchesHexPattern(pubKey, vanityPattern, vanityLen, false);
        } else if (vanityMode == 2) {
            uint8_t halfLen = vanityLen / 2;
            matched = matchesHexPattern(pubKey, vanityPattern, halfLen, true) &&
                      matchesHexPattern(pubKey, vanityPattern + halfLen, vanityLen - halfLen, false);
        } else if (vanityMode >= 3 && vanityMode <= 5) {
            char npub[64];
            encode_npub(pubKey, npub);

            if (vanityMode == 3) {
                matched = matchesBech32Pattern(npub, vanityPattern, vanityLen, true);
            } else if (vanityMode == 4) {
                matched = matchesBech32Pattern(npub, vanityPattern, vanityLen, false);
            } else if (vanityMode == 5) {
                uint8_t halfLen = vanityLen / 2;
                matched = matchesBech32Pattern(npub, vanityPattern, halfLen, true) &&
                          matchesBech32Pattern(npub, vanityPattern + halfLen, vanityLen - halfLen, false);
            }
        }

        // If matched, store result
        if (matched && results[gid] == 0) {
            results[gid] = 1;

            // Store private key
            for (int i = 0; i < 32; i++) {
                privKeys[gid * 32 + i] = privKey[i];
            }

            // Store public key
            for (int i = 0; i < 32; i++) {
                pubKeys[gid * 32 + i] = pubKey[i];
            }

            break; // Found a match, stop checking
        }
    }
}
