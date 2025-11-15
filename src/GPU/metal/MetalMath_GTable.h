/*
 * Rummage - Metal GTable Operations for Fast Point Multiplication
 *
 * Copyright (c) 2025 rossbates
 * Based on VanitySearch by Jean Luc PONS
 */

#ifndef METAL_MATH_GTABLE_H
#define METAL_MATH_GTABLE_H

#include "MetalMath.h"
#include "MetalMath_EC.h"

// GTable configuration (matches CPU/CUDA implementation)
#define NUM_GTABLE_CHUNK 16
#define NUM_GTABLE_VALUE 65536
#define SIZE_GTABLE_POINT 32

// GTable contains precomputed multiples of G (generator point)
// Organized as 16 chunks, each containing 65536 points
// Point i in chunk j represents: (65536^j * i) * G

// =========================================================================
// GTable Point Lookup
// =========================================================================

// Load a point from GTable
inline void GTable_LoadPoint(
    thread ECPoint *p,
    device const uint8_t *gTableX,
    device const uint8_t *gTableY,
    uint32_t index
) {
    // Each point is stored as 32 bytes (256 bits) for x and y coordinates
    uint32_t offset = index * SIZE_GTABLE_POINT;

    // Load X coordinate
    Load256(p->x, gTableX + offset);

    // Load Y coordinate
    Load256(p->y, gTableY + offset);

    p->isZero = false;
}

// =========================================================================
// Fast Scalar Multiplication using GTable
// Multiply generator G by scalar k: R = k * G
// =========================================================================

inline void GTable_MultG(
    thread ECPoint *r,
    thread const uint64_t *k,
    device const uint8_t *gTableX,
    device const uint8_t *gTableY
) {
    ECPoint result, temp;
    EC_SetZero(&result);

    // Process k in 16-bit chunks (matching GTable organization)
    for (int chunk = 0; chunk < NUM_GTABLE_CHUNK; chunk++) {
        // Extract 16-bit value from k for this chunk
        int bit_offset = chunk * 16;
        int word = bit_offset / 64;
        int shift = bit_offset % 64;

        uint32_t chunk_value;
        if (shift <= 48) {
            // Value fits in one word
            chunk_value = (k[word] >> shift) & 0xFFFF;
        } else {
            // Value spans two words
            uint32_t lo = (k[word] >> shift) & 0xFFFF;
            uint32_t hi = (k[word + 1] << (64 - shift)) & 0xFFFF;
            chunk_value = lo | hi;
        }

        // Skip if chunk value is zero
        if (chunk_value == 0) continue;

        // Calculate GTable index: chunk_base + chunk_value
        uint32_t gtable_index = (chunk * NUM_GTABLE_VALUE) + chunk_value;

        // Load point from GTable
        GTable_LoadPoint(&temp, gTableX, gTableY, gtable_index);

        // Add to result
        ECPoint sum;
        EC_Add(&sum, &result, &temp);
        EC_Set(&result, &sum);
    }

    EC_Set(r, &result);
}

// =========================================================================
// Convert private key (256-bit) to public key (secp256k1 point)
// Public key = private_key * G
// Returns x-coordinate only (Schnorr/Nostr format)
// =========================================================================

inline void PrivKeyToPubKey(
    thread uint64_t *pubkey_x,
    thread const uint64_t *privkey,
    device const uint8_t *gTableX,
    device const uint8_t *gTableY
) {
    ECPoint pubkey;

    // Multiply generator G by private key
    GTable_MultG(&pubkey, privkey, gTableX, gTableY);

    // Extract x-coordinate
    Set256(pubkey_x, pubkey.x);
}

// =========================================================================
// Helper: Add two private keys (for sequential search)
// result = (a + b) mod ORDER
// =========================================================================

inline void PrivKeyAdd(
    thread uint64_t *result,
    thread const uint64_t *a,
    thread const uint64_t *b
) {
    Add256(result, a, b);

    // Reduce modulo group order if needed
    bool greater_or_equal = false;

    if (result[4] > _ORDER[4]) greater_or_equal = true;
    else if (result[4] == _ORDER[4]) {
        if (result[3] > _ORDER[3]) greater_or_equal = true;
        else if (result[3] == _ORDER[3]) {
            if (result[2] > _ORDER[2]) greater_or_equal = true;
            else if (result[2] == _ORDER[2]) {
                if (result[1] > _ORDER[1]) greater_or_equal = true;
                else if (result[1] == _ORDER[1]) {
                    if (result[0] >= _ORDER[0]) greater_or_equal = true;
                }
            }
        }
    }

    if (greater_or_equal) {
        Sub256(result, result, _ORDER);
    }
}

// =========================================================================
// Helper: Increment a 256-bit integer (for sequential iteration)
// =========================================================================

inline void Increment256(thread uint64_t *val) {
    uint64_t one[5];
    SetInt32(one, 1);
    Add256(val, val, one);
}

#endif // METAL_MATH_GTABLE_H
