/*
 * Rummage - Metal Elliptic Curve Operations for secp256k1
 *
 * Copyright (c) 2025 rossbates
 * Based on VanitySearch by Jean Luc PONS
 */

#ifndef METAL_MATH_EC_H
#define METAL_MATH_EC_H

#include "MetalMath.h"
#include "MetalMath_ModArith.h"

// =========================================================================
// Elliptic Curve Point Structure
// =========================================================================

struct ECPoint {
    uint64_t x[5];
    uint64_t y[5];
    bool isZero; // Point at infinity flag
};

// =========================================================================
// Point Operations
// =========================================================================

// Check if point is at infinity
inline bool EC_IsZero(thread const ECPoint *p) {
    return p->isZero;
}

// Set point to zero (point at infinity)
inline void EC_SetZero(thread ECPoint *p) {
    SetZero256(p->x);
    SetZero256(p->y);
    p->isZero = true;
}

// Set point coordinates
inline void EC_Set(thread ECPoint *dst, thread const ECPoint *src) {
    Set256(dst->x, src->x);
    Set256(dst->y, src->y);
    dst->isZero = src->isZero;
}

// Check if two points are equal
inline bool EC_IsEqual(thread const ECPoint *a, thread const ECPoint *b) {
    if (a->isZero && b->isZero) return true;
    if (a->isZero || b->isZero) return false;
    return IsEqual256(a->x, b->x) && IsEqual256(a->y, b->y);
}

// =========================================================================
// Point Doubling: R = 2*P
// For secp256k1: y^2 = x^3 + 7
// Point doubling formula:
//   s = (3*x^2) / (2*y)
//   x' = s^2 - 2*x
//   y' = s*(x - x') - y
// =========================================================================

inline void EC_Double(thread ECPoint *r, thread const ECPoint *p) {
    if (p->isZero) {
        EC_SetZero(r);
        return;
    }

    // Check if y == 0 (result is point at infinity)
    if (IsZero256(p->y)) {
        EC_SetZero(r);
        return;
    }

    uint64_t s[5], temp[5], temp2[5];
    uint64_t x_squared[5], three_x_squared[5];
    uint64_t two_y[5], inv_two_y[5];
    uint64_t new_x[5], new_y[5];

    // Compute slope s = (3*x^2) / (2*y)

    // x_squared = x^2
    ModSqr256(x_squared, p->x);

    // three_x_squared = 3 * x^2
    Add256(temp, x_squared, x_squared);     // 2*x^2
    ModAdd256(three_x_squared, temp, x_squared); // 3*x^2

    // two_y = 2 * y
    ModAdd256(two_y, p->y, p->y);

    // inv_two_y = (2*y)^(-1)
    ModInv256(inv_two_y, two_y);

    // s = (3*x^2) * (2*y)^(-1)
    ModMult256(s, three_x_squared, inv_two_y);

    // Compute new_x = s^2 - 2*x
    ModSqr256(temp, s);                    // s^2
    ModAdd256(temp2, p->x, p->x);          // 2*x
    ModSub256(new_x, temp, temp2);         // s^2 - 2*x

    // Compute new_y = s*(x - new_x) - y
    ModSub256(temp, p->x, new_x);          // x - new_x
    ModMult256(temp2, s, temp);            // s * (x - new_x)
    ModSub256(new_y, temp2, p->y);         // s*(x - new_x) - y

    // Set result
    Set256(r->x, new_x);
    Set256(r->y, new_y);
    r->isZero = false;
}

// =========================================================================
// Point Addition: R = P + Q
// Addition formula:
//   s = (y2 - y1) / (x2 - x1)
//   x3 = s^2 - x1 - x2
//   y3 = s*(x1 - x3) - y1
// =========================================================================

inline void EC_Add(thread ECPoint *r, thread const ECPoint *p, thread const ECPoint *q) {
    // Handle point at infinity
    if (p->isZero) {
        EC_Set(r, q);
        return;
    }
    if (q->isZero) {
        EC_Set(r, p);
        return;
    }

    // Check if points are equal (use doubling instead)
    if (EC_IsEqual(p, q)) {
        EC_Double(r, p);
        return;
    }

    // Check if x coordinates are equal but y coordinates differ (result is infinity)
    if (IsEqual256(p->x, q->x)) {
        EC_SetZero(r);
        return;
    }

    uint64_t s[5], temp[5], temp2[5];
    uint64_t dx[5], dy[5], inv_dx[5];
    uint64_t new_x[5], new_y[5];

    // Compute slope s = (y2 - y1) / (x2 - x1)

    // dy = y2 - y1
    ModSub256(dy, q->y, p->y);

    // dx = x2 - x1
    ModSub256(dx, q->x, p->x);

    // inv_dx = dx^(-1)
    ModInv256(inv_dx, dx);

    // s = dy * dx^(-1)
    ModMult256(s, dy, inv_dx);

    // Compute new_x = s^2 - x1 - x2
    ModSqr256(temp, s);                    // s^2
    ModSub256(temp2, temp, p->x);          // s^2 - x1
    ModSub256(new_x, temp2, q->x);         // s^2 - x1 - x2

    // Compute new_y = s*(x1 - new_x) - y1
    ModSub256(temp, p->x, new_x);          // x1 - new_x
    ModMult256(temp2, s, temp);            // s * (x1 - new_x)
    ModSub256(new_y, temp2, p->y);         // s*(x1 - new_x) - y1

    // Set result
    Set256(r->x, new_x);
    Set256(r->y, new_y);
    r->isZero = false;
}

// =========================================================================
// Scalar Multiplication using Double-and-Add
// R = k * P
// =========================================================================

inline void EC_Mult(thread ECPoint *r, thread const ECPoint *p, thread const uint64_t *k) {
    ECPoint result, temp;
    EC_SetZero(&result);
    EC_Set(&temp, p);

    // Process each bit of k
    for (int i = 0; i < 256; i++) {
        int word = i / 64;
        int bit = i % 64;

        // If bit is set, add temp to result
        if ((k[word] >> bit) & 1) {
            ECPoint sum;
            EC_Add(&sum, &result, &temp);
            EC_Set(&result, &sum);
        }

        // Double temp
        ECPoint doubled;
        EC_Double(&doubled, &temp);
        EC_Set(&temp, &doubled);
    }

    EC_Set(r, &result);
}

// =========================================================================
// secp256k1 Specific: Check if point is on curve
// y^2 = x^3 + 7 (mod P)
// =========================================================================

inline bool EC_IsOnCurve(thread const ECPoint *p) {
    if (p->isZero) return true;

    uint64_t y_squared[5], x_cubed[5], x_squared[5];
    uint64_t rhs[5];

    // Compute y^2
    ModSqr256(y_squared, p->y);

    // Compute x^3
    ModSqr256(x_squared, p->x);
    ModMult256(x_cubed, x_squared, p->x);

    // Compute x^3 + 7
    uint64_t seven[5];
    SetInt32(seven, 7);
    ModAdd256(rhs, x_cubed, seven);

    // Check if y^2 == x^3 + 7
    return IsEqual256(y_squared, rhs);
}

// =========================================================================
// Get Y coordinate from X (for compressed public keys)
// Given x, compute y from y^2 = x^3 + 7
// Returns the even y coordinate
// =========================================================================

inline bool EC_GetY(thread uint64_t *y, thread const uint64_t *x, bool odd) {
    uint64_t x_cubed[5], x_squared[5], y_squared[5];
    uint64_t seven[5];

    // Compute x^3 + 7
    ModSqr256(x_squared, x);
    ModMult256(x_cubed, x_squared, x);
    SetInt32(seven, 7);
    ModAdd256(y_squared, x_cubed, seven);

    // Compute square root using Tonelli-Shanks or Fermat's method
    // For secp256k1 prime, we can use: y = (y_squared)^((P+1)/4) mod P
    // Since P ≡ 3 (mod 4)

    uint64_t exp[5] = {
        0xFFFFFFFFBFFFFF0CULL,  // (P+1)/4 for secp256k1
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL,
        0x3FFFFFFFFFFFFFFFULL,
        0ULL
    };

    ModExp256(y, y_squared, exp);

    // Check if we need the odd or even root
    bool y_is_odd = (y[0] & 1) != 0;
    if (y_is_odd != odd) {
        ModNeg256(y, y);
    }

    return true;
}

#endif // METAL_MATH_EC_H
