/*
 * Rummage - Metal Modular Arithmetic for secp256k1
 *
 * Copyright (c) 2025 rossbates
 * Based on VanitySearch by Jean Luc PONS
 */

#ifndef METAL_MATH_MODARITH_H
#define METAL_MATH_MODARITH_H

#include "MetalMath.h"

// =========================================================================
// 256-bit Multiplication
// =========================================================================

// 64x64 -> 128-bit multiplication
inline void Mult64(uint64_t a, uint64_t b, thread uint64_t *hi, thread uint64_t *lo) {
    // Use 128-bit multiplication if available
    #ifdef __HAVE_NATIVE_WIDE_OPERATIONS__
    __uint128_t product = (__uint128_t)a * b;
    *lo = (uint64_t)product;
    *hi = (uint64_t)(product >> 64);
    #else
    // Fallback: Break into 32-bit parts
    uint64_t a_lo = a & 0xFFFFFFFFULL;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFFULL;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t carry = ((p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL)) >> 32;

    *lo = p0 + (p1 << 32) + (p2 << 32);
    *hi = p3 + (p1 >> 32) + (p2 >> 32) + carry;
    #endif
}

// 256-bit multiplication (schoolbook algorithm)
inline void Mult256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *b) {
    uint64_t result[8] = {0}; // Need 512 bits for full product
    uint64_t hi, lo;

    // Multiply each pair of words
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            Mult64(a[i], b[j], &hi, &lo);

            // Add to result
            uint64_t carry = 0;
            int idx = i + j;

            // Add lo
            uint64_t sum = result[idx] + lo;
            result[idx] = sum;
            carry = (sum < result[idx]) ? 1 : 0;

            // Add hi with carry
            idx++;
            sum = result[idx] + hi + carry;
            result[idx] = sum;
            carry = (sum < result[idx]) ? 1 : ((sum == result[idx] && carry) ? 1 : 0);

            // Propagate carry
            while (carry && idx < 7) {
                idx++;
                sum = result[idx] + carry;
                result[idx] = sum;
                carry = (sum < result[idx]) ? 1 : 0;
            }
        }
    }

    // Copy lower 256 bits to result (for modular multiplication, we'll reduce later)
    r[0] = result[0];
    r[1] = result[1];
    r[2] = result[2];
    r[3] = result[3];
    r[4] = result[4]; // Overflow word
}

// =========================================================================
// Modular Arithmetic
// =========================================================================

// Modular addition: r = (a + b) mod P
inline void ModAdd256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *b) {
    Add256(r, a, b);

    // If result >= P, subtract P
    // Compare with P
    bool greater = false;
    if (r[4] > _P[4]) greater = true;
    else if (r[4] == _P[4]) {
        if (r[3] > _P[3]) greater = true;
        else if (r[3] == _P[3]) {
            if (r[2] > _P[2]) greater = true;
            else if (r[2] == _P[2]) {
                if (r[1] > _P[1]) greater = true;
                else if (r[1] == _P[1]) {
                    if (r[0] >= _P[0]) greater = true;
                }
            }
        }
    }

    if (greater) {
        SubP(r);
    }
}

// Modular subtraction: r = (a - b) mod P
inline void ModSub256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *b) {
    Sub256(r, a, b);

    // If result is negative, add P
    if (IsNegative256(r)) {
        AddP(r);
    }
}

// Modular negation: r = -a mod P = P - a
inline void ModNeg256(thread uint64_t *r, thread const uint64_t *a) {
    if (IsZero256(a)) {
        SetZero256(r);
    } else {
        Sub256(r, _P, a);
    }
}

// Simple modular reduction (not optimized)
inline void ModReduce256(thread uint64_t *r) {
    // Repeatedly subtract P while r >= P
    while (!IsNegative256(r)) {
        bool greater_or_equal = false;

        if (r[4] > _P[4]) greater_or_equal = true;
        else if (r[4] == _P[4]) {
            if (r[3] > _P[3]) greater_or_equal = true;
            else if (r[3] == _P[3]) {
                if (r[2] > _P[2]) greater_or_equal = true;
                else if (r[2] == _P[2]) {
                    if (r[1] > _P[1]) greater_or_equal = true;
                    else if (r[1] == _P[1]) {
                        if (r[0] >= _P[0]) greater_or_equal = true;
                    }
                }
            }
        }

        if (!greater_or_equal) break;
        SubP(r);
    }

    // Handle negative results
    if (IsNegative256(r)) {
        AddP(r);
    }
}

// Modular multiplication (simple version - can be optimized with Montgomery)
inline void ModMult256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *b) {
    // For now, use simple multiply and reduce
    // TODO: Optimize with Montgomery multiplication in future
    Mult256(r, a, b);
    ModReduce256(r);
}

// Modular squaring: r = a^2 mod P
inline void ModSqr256(thread uint64_t *r, thread const uint64_t *a) {
    ModMult256(r, a, a);
}

// Modular exponentiation: r = a^e mod P (using square-and-multiply)
inline void ModExp256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *e) {
    uint64_t result[5];
    uint64_t base[5];

    SetInt32(result, 1); // result = 1
    Set256(base, a);     // base = a

    // Process each bit of exponent
    for (int i = 0; i < 256; i++) {
        int word = i / 64;
        int bit = i % 64;

        // If bit is set, multiply result by base
        if ((e[word] >> bit) & 1) {
            ModMult256(result, result, base);
        }

        // Square base
        ModSqr256(base, base);
    }

    Set256(r, result);
}

// Modular inverse using Fermat's little theorem: a^(P-2) mod P
// For secp256k1, P is prime, so a^(-1) = a^(P-2) mod P
inline void ModInv256(thread uint64_t *r, thread const uint64_t *a) {
    // P - 2 for secp256k1
    uint64_t exp[5] = {
        0xFFFFFFFEFFFFFC2DULL,  // P[0] - 2
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL,
        0ULL
    };

    ModExp256(r, a, exp);
}

#endif // METAL_MATH_MODARITH_H
