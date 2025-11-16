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
// Fast Scalar Multiplication using GTable with Jacobian coordinates
// Multiply generator G by scalar k: R = k * G
// Uses Jacobian coordinates to avoid modular inverse until the very end!
// =========================================================================

inline void GTable_MultG_Jacobian(
    thread ECPointJacobian *r,
    thread const uint64_t *k,
    device const uint8_t *gTableX,
    device const uint8_t *gTableY
) {
    ECPointJacobian result;
    ECPoint temp_affine;
    ECJ_SetZero(&result);

    // Process k in 16-bit chunks (matching GTable organization)
    // GTable stores: index i in chunk j = (65536^j * (i+1)) * G
    // So we need to use (chunk_value - 1) as the array index
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

        // Skip if chunk value is zero (means don't add this chunk)
        if (chunk_value == 0) continue;

        // Calculate GTable index: chunk_base + (chunk_value - 1)
        // Subtract 1 because GTable[0] in chunk j represents 1*(65536^j)*G, not 0
        uint32_t gtable_index = (chunk * NUM_GTABLE_VALUE) + (chunk_value - 1);

        // Load point from GTable (in affine coordinates)
        GTable_LoadPoint(&temp_affine, gTableX, gTableY, gtable_index);

        // Add to result using mixed Jacobian-affine addition (no division!)
        ECPointJacobian sum;
        ECJ_AddMixed(&sum, &result, &temp_affine);

        // Copy result
        result.X[0] = sum.X[0]; result.X[1] = sum.X[1]; result.X[2] = sum.X[2]; result.X[3] = sum.X[3]; result.X[4] = sum.X[4];
        result.Y[0] = sum.Y[0]; result.Y[1] = sum.Y[1]; result.Y[2] = sum.Y[2]; result.Y[3] = sum.Y[3]; result.Y[4] = sum.Y[4];
        result.Z[0] = sum.Z[0]; result.Z[1] = sum.Z[1]; result.Z[2] = sum.Z[2]; result.Z[3] = sum.Z[3]; result.Z[4] = sum.Z[4];
        result.isZero = sum.isZero;
    }

    // Copy final result
    r->X[0] = result.X[0]; r->X[1] = result.X[1]; r->X[2] = result.X[2]; r->X[3] = result.X[3]; r->X[4] = result.X[4];
    r->Y[0] = result.Y[0]; r->Y[1] = result.Y[1]; r->Y[2] = result.Y[2]; r->Y[3] = result.Y[3]; r->Y[4] = result.Y[4];
    r->Z[0] = result.Z[0]; r->Z[1] = result.Z[1]; r->Z[2] = result.Z[2]; r->Z[3] = result.Z[3]; r->Z[4] = result.Z[4];
    r->isZero = result.isZero;
}

// =========================================================================
// Convert private key (256-bit) to public key (secp256k1 point)
// Public key = private_key * G
// Returns x-coordinate only (Schnorr/Nostr format)
// Uses Jacobian coordinates - only one modular inverse at the end!
// =========================================================================

inline void PrivKeyToPubKey(
    thread uint64_t *pubkey_x,
    thread const uint64_t *privkey,
    device const uint8_t *gTableX,
    device const uint8_t *gTableY
) {
    // Multiply generator G by private key (in Jacobian coordinates)
    ECPointJacobian pubkey_jac;
    GTable_MultG_Jacobian(&pubkey_jac, privkey, gTableX, gTableY);

    // Convert to affine coordinates (requires ONE modular inverse)
    ECPoint pubkey_affine;
    ECJ_ToAffine(&pubkey_affine, &pubkey_jac);

    // Extract x-coordinate
    Set256(pubkey_x, pubkey_affine.x);
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
