/*
 * Rummage - Metal Math Library for secp256k1
 *
 * Copyright (c) 2025 rossbates
 * Based on VanitySearch by Jean Luc PONS
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#ifndef METAL_MATH_H
#define METAL_MATH_H

#include <metal_stdlib>
using namespace metal;

// 256-bit integer is represented as 5 x 64-bit words
// This gives us 320 bits total, with the 5th word used for overflow/sign
#define NBBLOCK 5

// secp256k1 prime: P = 2^256 - 2^32 - 2^9 - 2^8 - 2^7 - 2^6 - 2^4 - 1
constant uint64_t _P[5] = {
    0xFFFFFFFEFFFFFC2FULL,
    0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL,
    0ULL
};

// secp256k1 order (group order)
constant uint64_t _ORDER[5] = {
    0xBFD25E8CD0364141ULL,
    0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL,
    0ULL
};

// Generator point G coordinates (secp256k1)
constant uint64_t _Gx[5] = {
    0x59F2815B16F81798ULL,
    0x029BFCDB2DCE28D9ULL,
    0x55A06295CE870B07ULL,
    0x79BE667EF9DCBBACULL,
    0ULL
};

constant uint64_t _Gy[5] = {
    0x9C47D08FFB10D4B8ULL,
    0xFD17B448A6855419ULL,
    0x5DA4FBFC0E1108A8ULL,
    0x483ADA7726A3C465ULL,
    0ULL
};

// 64-bit LSB negative inverse of P (mod 2^64) for Montgomery multiplication
#define MM64 0xD838091DD2253531ULL

// =========================================================================
// 256-bit Integer Basic Operations
// =========================================================================

// Check if zero
inline bool IsZero256(thread const uint64_t *a) {
    return (a[0] | a[1] | a[2] | a[3] | a[4]) == 0ULL;
}

// Check if one
inline bool IsOne256(thread const uint64_t *a) {
    return (a[0] == 1ULL) && (a[1] == 0ULL) && (a[2] == 0ULL) &&
           (a[3] == 0ULL) && (a[4] == 0ULL);
}

// Check equality
inline bool IsEqual256(thread const uint64_t *a, thread const uint64_t *b) {
    return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]) &&
           (a[3] == b[3]) && (a[4] == b[4]);
}

// Check if positive (sign bit clear)
inline bool IsPositive256(thread const uint64_t *x) {
    return ((int64_t)x[4]) >= 0LL;
}

// Check if negative (sign bit set)
inline bool IsNegative256(thread const uint64_t *x) {
    return ((int64_t)x[4]) < 0LL;
}

// Load from memory
inline void Load256(thread uint64_t *dst, device const uint8_t *src) {
    // Load little-endian bytes into uint64_t array
    for (int i = 0; i < 4; i++) {
        dst[i] = 0;
        for (int j = 0; j < 8; j++) {
            dst[i] |= ((uint64_t)src[i * 8 + j]) << (j * 8);
        }
    }
    dst[4] = 0;
}

// Store to memory
inline void Store256(device uint8_t *dst, thread const uint64_t *src) {
    // Store uint64_t array as little-endian bytes
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            dst[i * 8 + j] = (uint8_t)(src[i] >> (j * 8));
        }
    }
}

// Set value
inline void Set256(thread uint64_t *dst, thread const uint64_t *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
}

// Set to zero
inline void SetZero256(thread uint64_t *dst) {
    dst[0] = 0;
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    dst[4] = 0;
}

// Set to constant value
inline void SetInt32(thread uint64_t *dst, uint32_t val) {
    dst[0] = val;
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    dst[4] = 0;
}

// =========================================================================
// 256-bit Addition and Subtraction with Carry
// =========================================================================

// Add with carry implementation
inline void Add256_impl(thread uint64_t *r, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                        uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3, uint64_t b4) {
    uint64_t carry = 0;
    uint64_t sum;

    // Portable version
    sum = a0 + b0;
    r[0] = sum;
    carry = (sum < a0) ? 1 : 0;

    sum = a1 + b1 + carry;
    r[1] = sum;
    uint64_t new_carry = (sum < a1) ? 1 : ((sum == a1 && carry) ? 1 : 0);
    carry = new_carry;

    sum = a2 + b2 + carry;
    r[2] = sum;
    new_carry = (sum < a2) ? 1 : ((sum == a2 && carry) ? 1 : 0);
    carry = new_carry;

    sum = a3 + b3 + carry;
    r[3] = sum;
    new_carry = (sum < a3) ? 1 : ((sum == a3 && carry) ? 1 : 0);
    carry = new_carry;

    r[4] = a4 + b4 + carry;
}

inline void Add256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *b) {
    Add256_impl(r, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
}

inline void Add256(thread uint64_t *r, constant const uint64_t *a, thread const uint64_t *b) {
    Add256_impl(r, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
}

inline void Add256(thread uint64_t *r, thread const uint64_t *a, constant const uint64_t *b) {
    Add256_impl(r, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
}

// Subtract with borrow (overloaded for different address spaces)
inline void Sub256_impl(thread uint64_t *r, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                        uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3, uint64_t b4) {
    uint64_t borrow = 0;
    uint64_t diff;

    diff = a0 - b0;
    r[0] = diff;
    borrow = (diff > a0) ? 1 : 0;

    uint64_t temp = a1 - borrow;
    borrow = (temp > a1) ? 1 : 0;
    diff = temp - b1;
    r[1] = diff;
    borrow |= (diff > temp) ? 1 : 0;

    temp = a2 - borrow;
    borrow = (temp > a2) ? 1 : 0;
    diff = temp - b2;
    r[2] = diff;
    borrow |= (diff > temp) ? 1 : 0;

    temp = a3 - borrow;
    borrow = (temp > a3) ? 1 : 0;
    diff = temp - b3;
    r[3] = diff;
    borrow |= (diff > temp) ? 1 : 0;

    r[4] = a4 - b4 - borrow;
}

inline void Sub256(thread uint64_t *r, thread const uint64_t *a, thread const uint64_t *b) {
    Sub256_impl(r, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
}

inline void Sub256(thread uint64_t *r, constant const uint64_t *a, thread const uint64_t *b) {
    Sub256_impl(r, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
}

inline void Sub256(thread uint64_t *r, thread const uint64_t *a, constant const uint64_t *b) {
    Sub256_impl(r, a[0], a[1], a[2], a[3], a[4], b[0], b[1], b[2], b[3], b[4]);
}

// Add P (the secp256k1 prime)
inline void AddP(thread uint64_t *r) {
    uint64_t a[5];
    Set256(a, r);
    Add256(r, a, _P);
}

// Subtract P
inline void SubP(thread uint64_t *r) {
    uint64_t a[5];
    Set256(a, r);
    Sub256(r, a, _P);
}

// =========================================================================
// 256-bit Shift Operations
// =========================================================================

// Right shift
inline void ShiftR256(thread uint64_t *r, uint32_t n) {
    if (n == 0) return;
    if (n >= 256) {
        SetZero256(r);
        return;
    }

    uint32_t word_shift = n / 64;
    uint32_t bit_shift = n % 64;

    if (bit_shift == 0) {
        for (int i = 0; i < 5 - word_shift; i++) {
            r[i] = r[i + word_shift];
        }
        for (int i = 5 - word_shift; i < 5; i++) {
            r[i] = 0;
        }
    } else {
        for (int i = 0; i < 5 - word_shift - 1; i++) {
            r[i] = (r[i + word_shift] >> bit_shift) |
                   (r[i + word_shift + 1] << (64 - bit_shift));
        }
        r[5 - word_shift - 1] = r[4] >> bit_shift;
        for (int i = 5 - word_shift; i < 5; i++) {
            r[i] = 0;
        }
    }
}

// Left shift
inline void ShiftL256(thread uint64_t *r, uint32_t n) {
    if (n == 0) return;
    if (n >= 256) {
        SetZero256(r);
        return;
    }

    uint32_t word_shift = n / 64;
    uint32_t bit_shift = n % 64;

    if (bit_shift == 0) {
        for (int i = 4; i >= word_shift; i--) {
            r[i] = r[i - word_shift];
        }
        for (int i = 0; i < word_shift; i++) {
            r[i] = 0;
        }
    } else {
        for (int i = 4; i > word_shift; i--) {
            r[i] = (r[i - word_shift] << bit_shift) |
                   (r[i - word_shift - 1] >> (64 - bit_shift));
        }
        r[word_shift] = r[0] << bit_shift;
        for (int i = 0; i < word_shift; i++) {
            r[i] = 0;
        }
    }
}

#endif // METAL_MATH_H
