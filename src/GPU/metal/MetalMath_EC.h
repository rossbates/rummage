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
// Elliptic Curve Point Structures
// =========================================================================

// Affine coordinates (x, y)
struct ECPoint {
    uint64_t x[5];
    uint64_t y[5];
    bool isZero; // Point at infinity flag
};

// Jacobian coordinates (X, Y, Z) where x = X/Z^2, y = Y/Z^3
// This avoids division in point addition/doubling
struct ECPointJacobian {
    uint64_t X[5];
    uint64_t Y[5];
    uint64_t Z[5];
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

// =========================================================================
// Jacobian Coordinate Operations (Division-Free!)
// =========================================================================

// Set Jacobian point to zero (point at infinity)
inline void ECJ_SetZero(thread ECPointJacobian *p) {
    SetZero256(p->X);
    SetZero256(p->Y);
    SetZero256(p->Z);
    p->isZero = true;
}

// Convert affine to Jacobian: (x, y) -> (x, y, 1)
inline void ECJ_FromAffine(thread ECPointJacobian *jac, thread const ECPoint *aff) {
    if (aff->isZero) {
        ECJ_SetZero(jac);
    } else {
        Set256(jac->X, aff->x);
        Set256(jac->Y, aff->y);
        SetInt32(jac->Z, 1);
        jac->isZero = false;
    }
}

// Convert Jacobian to affine: (X, Y, Z) -> (X/Z^2, Y/Z^3)
// WARNING: This requires modular inverse! Only use at the very end.
inline void ECJ_ToAffine(thread ECPoint *aff, thread const ECPointJacobian *jac) {
    if (jac->isZero) {
        EC_SetZero(aff);
        return;
    }

    // Compute Z^(-1), Z^(-2), Z^(-3)
    uint64_t Z_inv[5], Z_inv2[5], Z_inv3[5];

    ModInv256(Z_inv, jac->Z);      // Z^(-1)
    ModMult256(Z_inv2, Z_inv, Z_inv);  // Z^(-2)
    ModMult256(Z_inv3, Z_inv2, Z_inv); // Z^(-3)

    // x = X * Z^(-2)
    ModMult256(aff->x, jac->X, Z_inv2);

    // y = Y * Z^(-3)
    ModMult256(aff->y, jac->Y, Z_inv3);

    aff->isZero = false;
}

// Jacobian point doubling: R = 2*P (no division!)
// Formula from: http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian.html
// For y^2 = x^3 + 7:
//   S = 4*X*Y^2
//   M = 3*X^2
//   X' = M^2 - 2*S
//   Y' = M*(S - X') - 8*Y^4
//   Z' = 2*Y*Z
inline void ECJ_Double(thread ECPointJacobian *r, thread const ECPointJacobian *p) {
    if (p->isZero) {
        ECJ_SetZero(r);
        return;
    }

    uint64_t S[5], M[5], T[5];
    uint64_t Y2[5], Y4[5], X2[5];
    uint64_t newX[5], newY[5], newZ[5];

    // Y2 = Y^2
    ModMult256(Y2, p->Y, p->Y);

    // S = 4*X*Y^2
    ModMult256(T, p->X, Y2);
    ModAdd256(S, T, T);      // 2*X*Y^2
    ModAdd256(S, S, S);      // 4*X*Y^2

    // M = 3*X^2 (for secp256k1, a=0 so we don't add 3*Z^4)
    ModMult256(X2, p->X, p->X);
    ModAdd256(M, X2, X2);    // 2*X^2
    ModAdd256(M, M, X2);     // 3*X^2

    // X' = M^2 - 2*S
    ModMult256(newX, M, M);
    ModSub256(newX, newX, S);
    ModSub256(newX, newX, S);

    // Y' = M*(S - X') - 8*Y^4
    ModSub256(T, S, newX);
    ModMult256(newY, M, T);
    ModMult256(Y4, Y2, Y2);  // Y^4
    ModAdd256(T, Y4, Y4);    // 2*Y^4
    ModAdd256(T, T, T);      // 4*Y^4
    ModAdd256(T, T, T);      // 8*Y^4
    ModSub256(newY, newY, T);

    // Z' = 2*Y*Z
    ModMult256(newZ, p->Y, p->Z);
    ModAdd256(newZ, newZ, newZ);

    Set256(r->X, newX);
    Set256(r->Y, newY);
    Set256(r->Z, newZ);
    r->isZero = false;
}

// Jacobian point addition: R = P + Q (no division!)
// Mixed addition (P in Jacobian, Q in affine with Z=1)
// Formula from: http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian.html
inline void ECJ_AddMixed(thread ECPointJacobian *r, thread const ECPointJacobian *p, thread const ECPoint *q) {
    if (p->isZero) {
        ECJ_FromAffine(r, q);
        return;
    }
    if (q->isZero) {
        r->X[0] = p->X[0]; r->X[1] = p->X[1]; r->X[2] = p->X[2]; r->X[3] = p->X[3]; r->X[4] = p->X[4];
        r->Y[0] = p->Y[0]; r->Y[1] = p->Y[1]; r->Y[2] = p->Y[2]; r->Y[3] = p->Y[3]; r->Y[4] = p->Y[4];
        r->Z[0] = p->Z[0]; r->Z[1] = p->Z[1]; r->Z[2] = p->Z[2]; r->Z[3] = p->Z[3]; r->Z[4] = p->Z[4];
        r->isZero = p->isZero;
        return;
    }

    uint64_t Z2[5], U2[5], S2[5], H[5], HH[5], I[5], J[5], V[5];
    uint64_t newX[5], newY[5], newZ[5], T[5];

    // Z2 = Z1^2
    ModMult256(Z2, p->Z, p->Z);

    // U2 = X2*Z1^2
    ModMult256(U2, q->x, Z2);

    // S2 = Y2*Z1^3
    ModMult256(T, Z2, p->Z);
    ModMult256(S2, q->y, T);

    // H = U2 - X1
    ModSub256(H, U2, p->X);

    // Check if points are equal (H == 0)
    if (IsZero256(H)) {
        // If S2 == Y1, points are equal -> double
        // If S2 != Y1, result is point at infinity
        uint64_t diff[5];
        ModSub256(diff, S2, p->Y);
        if (IsZero256(diff)) {
            ECJ_Double(r, p);
            return;
        } else {
            ECJ_SetZero(r);
            return;
        }
    }

    // I = (2*H)^2
    ModAdd256(T, H, H);
    ModMult256(I, T, T);

    // J = H*I
    ModMult256(J, H, I);

    // V = X1*I
    ModMult256(V, p->X, I);

    // X3 = r^2 - J - 2*V where r = 2*(S2 - Y1)
    ModSub256(T, S2, p->Y);
    ModAdd256(T, T, T);  // r = 2*(S2 - Y1)
    ModMult256(newX, T, T);
    ModSub256(newX, newX, J);
    ModSub256(newX, newX, V);
    ModSub256(newX, newX, V);

    // Y3 = r*(V - X3) - 2*Y1*J
    ModSub256(HH, V, newX);
    ModSub256(T, S2, p->Y);
    ModAdd256(T, T, T);  // r again
    ModMult256(newY, T, HH);
    ModMult256(T, p->Y, J);
    ModAdd256(T, T, T);
    ModSub256(newY, newY, T);

    // Z3 = Z1*2*H
    ModAdd256(T, H, H);
    ModMult256(newZ, p->Z, T);

    Set256(r->X, newX);
    Set256(r->Y, newY);
    Set256(r->Z, newZ);
    r->isZero = false;
}

#endif // METAL_MATH_EC_H
