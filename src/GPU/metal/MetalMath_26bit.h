/*
 * Rummage - Metal Math Library using 26-bit limbs
 *
 * Copyright (c) 2025 rossbates
 *
 * This implementation uses 10x26-bit limbs to represent 256-bit integers.
 * This approach:
 * - Reduces carry propagation complexity (26 bits in 32-bit leaves 6 bits headroom)
 * - Maps better to Metal's vectorized operations
 * - Avoids deep nested loops that cause GPU timeouts
 *
 * Inspired by libsecp256k1's field element representation.
 */

#ifndef METAL_MATH_26BIT_H
#define METAL_MATH_26BIT_H

#include <metal_stdlib>
using namespace metal;

// =========================================================================
// 26-bit Limb Representation
// =========================================================================

// 256-bit integer represented as 10 limbs of 26 bits each
// limbs[0] is LSB, limbs[9] is MSB
// Total: 10 * 26 = 260 bits (4 bits unused in MSB for overflow)

typedef struct {
    uint32_t limbs[10];
} uint256_26;

// Mask for 26-bit values
#define MASK26 0x3FFFFFF  // 2^26 - 1

// =========================================================================
// secp256k1 Prime P in 26-bit limbs
// P = 2^256 - 2^32 - 977
// P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
// =========================================================================

constant uint32_t P_26[10] = {
    0x3FFFC2F,  // bits 0-25
    0x3FFFFBF,  // bits 26-51
    0x3FFFFFF,  // bits 52-77
    0x3FFFFFF,  // bits 78-103
    0x3FFFFFF,  // bits 104-129
    0x3FFFFFF,  // bits 130-155
    0x3FFFFFF,  // bits 156-181
    0x3FFFFFF,  // bits 182-207
    0x3FFFFFF,  // bits 208-233
    0x00003FF   // bits 234-259 (only 22 bits used)
};

// =========================================================================
// Basic Operations
// =========================================================================

// Set to zero
inline void set_zero_26(thread uint256_26 *a) {
    for (int i = 0; i < 10; i++) {
        a->limbs[i] = 0;
    }
}

// Set from small integer
inline void set_int_26(thread uint256_26 *a, uint32_t val) {
    a->limbs[0] = val & MASK26;
    a->limbs[1] = (val >> 26) & MASK26;
    for (int i = 2; i < 10; i++) {
        a->limbs[i] = 0;
    }
}

// Copy
inline void copy_26(thread uint256_26 *dst, thread const uint256_26 *src) {
    for (int i = 0; i < 10; i++) {
        dst->limbs[i] = src->limbs[i];
    }
}

// Check if zero
inline bool is_zero_26(thread const uint256_26 *a) {
    uint32_t result = 0;
    for (int i = 0; i < 10; i++) {
        result |= a->limbs[i];
    }
    return result == 0;
}

// =========================================================================
// Conversion to/from 32-byte representation
// =========================================================================

// Load from 32-byte big-endian array
inline void load_26(thread uint256_26 *dst, thread const uint8_t *src) {
    // Convert 32 bytes to 10x26-bit limbs
    // Read in big-endian order

    uint32_t t0 = (uint32_t)src[31] | ((uint32_t)src[30] << 8) | ((uint32_t)src[29] << 16) | ((uint32_t)src[28] << 24);
    uint32_t t1 = (uint32_t)src[27] | ((uint32_t)src[26] << 8) | ((uint32_t)src[25] << 16) | ((uint32_t)src[24] << 24);
    uint32_t t2 = (uint32_t)src[23] | ((uint32_t)src[22] << 8) | ((uint32_t)src[21] << 16) | ((uint32_t)src[20] << 24);
    uint32_t t3 = (uint32_t)src[19] | ((uint32_t)src[18] << 8) | ((uint32_t)src[17] << 16) | ((uint32_t)src[16] << 24);
    uint32_t t4 = (uint32_t)src[15] | ((uint32_t)src[14] << 8) | ((uint32_t)src[13] << 16) | ((uint32_t)src[12] << 24);
    uint32_t t5 = (uint32_t)src[11] | ((uint32_t)src[10] << 8) | ((uint32_t)src[9] << 16) | ((uint32_t)src[8] << 24);
    uint32_t t6 = (uint32_t)src[7] | ((uint32_t)src[6] << 8) | ((uint32_t)src[5] << 16) | ((uint32_t)src[4] << 24);
    uint32_t t7 = (uint32_t)src[3] | ((uint32_t)src[2] << 8) | ((uint32_t)src[1] << 16) | ((uint32_t)src[0] << 24);

    // Pack into 26-bit limbs
    dst->limbs[0] = t0 & MASK26;
    dst->limbs[1] = ((t0 >> 26) | (t1 << 6)) & MASK26;
    dst->limbs[2] = ((t1 >> 20) | (t2 << 12)) & MASK26;
    dst->limbs[3] = ((t2 >> 14) | (t3 << 18)) & MASK26;
    dst->limbs[4] = ((t3 >> 8) | (t4 << 24)) & MASK26;
    dst->limbs[5] = (t4 >> 2) & MASK26;
    dst->limbs[6] = ((t4 >> 28) | (t5 << 4)) & MASK26;
    dst->limbs[7] = ((t5 >> 22) | (t6 << 10)) & MASK26;
    dst->limbs[8] = ((t6 >> 16) | (t7 << 16)) & MASK26;
    dst->limbs[9] = (t7 >> 10) & MASK26;
}

// Load from device memory
inline void load_26_device(thread uint256_26 *dst, device const uint8_t *src) {
    uint8_t temp[32];
    for (int i = 0; i < 32; i++) {
        temp[i] = src[i];
    }
    load_26(dst, temp);
}

// Store to 32-byte big-endian array
inline void store_26(thread uint8_t *dst, thread const uint256_26 *src) {
    // Unpack 26-bit limbs to bytes
    uint32_t t0 = src->limbs[0] | (src->limbs[1] << 26);
    uint32_t t1 = (src->limbs[1] >> 6) | (src->limbs[2] << 20);
    uint32_t t2 = (src->limbs[2] >> 12) | (src->limbs[3] << 14);
    uint32_t t3 = (src->limbs[3] >> 18) | (src->limbs[4] << 8);
    uint32_t t4 = (src->limbs[4] >> 24) | (src->limbs[5] << 2) | (src->limbs[6] << 28);
    uint32_t t5 = (src->limbs[6] >> 4) | (src->limbs[7] << 22);
    uint32_t t6 = (src->limbs[7] >> 10) | (src->limbs[8] << 16);
    uint32_t t7 = (src->limbs[8] >> 16) | (src->limbs[9] << 10);

    // Write in big-endian order
    dst[31] = t0; dst[30] = t0 >> 8; dst[29] = t0 >> 16; dst[28] = t0 >> 24;
    dst[27] = t1; dst[26] = t1 >> 8; dst[25] = t1 >> 16; dst[24] = t1 >> 24;
    dst[23] = t2; dst[22] = t2 >> 8; dst[21] = t2 >> 16; dst[20] = t2 >> 24;
    dst[19] = t3; dst[18] = t3 >> 8; dst[17] = t3 >> 16; dst[16] = t3 >> 24;
    dst[15] = t4; dst[14] = t4 >> 8; dst[13] = t4 >> 16; dst[12] = t4 >> 24;
    dst[11] = t5; dst[10] = t5 >> 8; dst[9] = t5 >> 16; dst[8] = t5 >> 24;
    dst[7] = t6; dst[6] = t6 >> 8; dst[5] = t6 >> 16; dst[4] = t6 >> 24;
    dst[3] = t7; dst[2] = t7 >> 8; dst[1] = t7 >> 16; dst[0] = t7 >> 24;
}

// =========================================================================
// Normalization - reduce limbs to 26 bits
// =========================================================================

inline void normalize_26(thread uint256_26 *a) {
    uint32_t carry = 0;

    #pragma unroll
    for (int i = 0; i < 9; i++) {
        uint32_t sum = a->limbs[i] + carry;
        a->limbs[i] = sum & MASK26;
        carry = sum >> 26;
    }
    a->limbs[9] += carry;
}

// =========================================================================
// Addition and Subtraction (with weak normalization)
// =========================================================================

// Add: r = a + b (weak reduction, may overflow limbs slightly)
inline void add_26(thread uint256_26 *r, thread const uint256_26 *a, thread const uint256_26 *b) {
    #pragma unroll
    for (int i = 0; i < 10; i++) {
        r->limbs[i] = a->limbs[i] + b->limbs[i];
    }
}

// Subtract: r = a - b (weak reduction)
inline void sub_26(thread uint256_26 *r, thread const uint256_26 *a, thread const uint256_26 *b) {
    #pragma unroll
    for (int i = 0; i < 10; i++) {
        r->limbs[i] = a->limbs[i] + (MASK26 * 2 + 1) - b->limbs[i];
    }
}

// =========================================================================
// Modular Reduction mod P
// =========================================================================

// Fast reduction modulo P using secp256k1 structure
// P = 2^256 - 2^32 - 977
inline void mod_p_26(thread uint256_26 *r) {
    normalize_26(r);

    // Check if r >= P and subtract P if needed
    // For now, simple repeated subtraction (max 2 iterations)
    for (int iter = 0; iter < 2; iter++) {
        // Check if r >= P
        bool gte = false;
        if (r->limbs[9] > P_26[9]) gte = true;
        else if (r->limbs[9] == P_26[9]) {
            for (int i = 8; i >= 0; i--) {
                if (r->limbs[i] > P_26[i]) {
                    gte = true;
                    break;
                } else if (r->limbs[i] < P_26[i]) {
                    break;
                }
            }
            if (!gte && r->limbs[0] >= P_26[0]) gte = true;
        }

        if (!gte) break;

        // Subtract P
        uint32_t borrow = 0;
        for (int i = 0; i < 10; i++) {
            uint32_t diff = r->limbs[i] + (MASK26 + 1) - P_26[i] - borrow;
            r->limbs[i] = diff & MASK26;
            borrow = (diff >> 26) ? 0 : 1;
        }
    }
}

// =========================================================================
// Modular Addition and Subtraction
// =========================================================================

inline void mod_add_26(thread uint256_26 *r, thread const uint256_26 *a, thread const uint256_26 *b) {
    add_26(r, a, b);
    normalize_26(r);
    mod_p_26(r);
}

inline void mod_sub_26(thread uint256_26 *r, thread const uint256_26 *a, thread const uint256_26 *b) {
    sub_26(r, a, b);
    normalize_26(r);
    mod_p_26(r);
}

// =========================================================================
// Multiplication (using Comba method with partial products)
// =========================================================================

// Multiply: r = a * b (produces up to 20 limbs, reduced mod P)
// Using optimized Comba multiplication for 26-bit limbs
inline void mod_mult_26(thread uint256_26 *r, thread const uint256_26 *a, thread const uint256_26 *b) {
    // Accumulator for partial products (need 64 bits for 26x26 + carries)
    uint64_t acc[20];

    // Initialize
    for (int i = 0; i < 20; i++) {
        acc[i] = 0;
    }

    // Compute partial products: unroll completely for Metal optimization
    #pragma unroll
    for (int i = 0; i < 10; i++) {
        #pragma unroll
        for (int j = 0; j < 10; j++) {
            acc[i + j] += (uint64_t)a->limbs[i] * (uint64_t)b->limbs[j];
        }
    }

    // Reduce carries (propagate to next limb)
    #pragma unroll
    for (int i = 0; i < 19; i++) {
        acc[i + 1] += acc[i] >> 26;
        acc[i] &= MASK26;
    }

    // Fast reduction for secp256k1: P = 2^256 - C where C = 2^32 + 977 = 0x1000003D1
    // For value x = x_high * 2^256 + x_low:
    //   x mod P = (x_low + x_high * C) mod P

    // C = 0x1000003D1 = 0x3D1 + (1 << 32)
    // In 26-bit limbs: C has limbs [0x3D1, 0x40, 0, ...]

    // Extract low 260 bits (10 limbs) and high part
    uint64_t low[10], high[10];

    for (int i = 0; i < 10; i++) {
        low[i] = acc[i];
    }
    for (int i = 0; i < 10; i++) {
        high[i] = (i < 10) ? acc[i + 10] : 0;
    }

    // Multiply high part by C = 0x1000003D1
    // C in 26-bit limbs: 0x3D1 in limb[0], 0x40 in limb[1]
    uint64_t c_mult[10] = {0};

    #pragma unroll
    for (int i = 0; i < 10; i++) {
        c_mult[i] += high[i] * 0x3D1;      // high[i] * 977
        if (i >= 1) {
            c_mult[i] += high[i-1] * 0x40;  // high[i-1] * (1<<32 in next limb)
        }
    }

    // Add to low part
    #pragma unroll
    for (int i = 0; i < 10; i++) {
        low[i] += c_mult[i];
    }

    // Propagate carries
    #pragma unroll
    for (int i = 0; i < 9; i++) {
        low[i+1] += low[i] >> 26;
        low[i] &= MASK26;
    }

    // Final reduction if needed (should be at most 1-2 iterations)
    for (int i = 0; i < 10; i++) {
        r->limbs[i] = (uint32_t)(low[i] & MASK26);
    }
    normalize_26(r);
    mod_p_26(r);
}

// Squaring (can be optimized but start with mult)
inline void mod_sqr_26(thread uint256_26 *r, thread const uint256_26 *a) {
    mod_mult_26(r, a, a);
}

// =========================================================================
// Modular Inverse using Extended Euclidean Algorithm
// =========================================================================

// Compare two 26-bit numbers: returns -1 if a < b, 0 if a == b, 1 if a > b
inline int cmp_26(thread const uint256_26 *a, thread const uint256_26 *b) {
    for (int i = 9; i >= 0; i--) {
        if (a->limbs[i] > b->limbs[i]) return 1;
        if (a->limbs[i] < b->limbs[i]) return -1;
    }
    return 0;
}

// Modular exponentiation: r = base^exp mod P
// Using square-and-multiply algorithm
inline void mod_exp_26(thread uint256_26 *r, thread const uint256_26 *base, thread const uint256_26 *exp) {
    set_int_26(r, 1);  // r = 1
    uint256_26 b_temp;
    copy_26(&b_temp, base);

    // Process each bit of exponent (from LSB to MSB)
    for (int i = 0; i < 260; i++) {  // 10 limbs * 26 bits = 260 bits
        int limb_idx = i / 26;
        int bit_idx = i % 26;

        // If bit is set, multiply result by base
        if ((exp->limbs[limb_idx] >> bit_idx) & 1) {
            uint256_26 temp;
            mod_mult_26(&temp, r, &b_temp);
            copy_26(r, &temp);
        }

        // Square the base (for next bit)
        if (i < 259) {  // Don't square on last iteration
            uint256_26 temp;
            mod_sqr_26(&temp, &b_temp);
            copy_26(&b_temp, &temp);
        }
    }
}

// Modular inverse using Fermat's Little Theorem: a^(P-2) mod P
// For secp256k1 prime, this is faster with 26-bit limbs than Extended Euclidean
inline void mod_inv_26(thread uint256_26 *r, thread const uint256_26 *a) {
    // P - 2 for secp256k1 in 26-bit limbs
    uint256_26 exp;
    exp.limbs[0] = 0x3FFFC2D;  // P[0] - 2
    exp.limbs[1] = 0x3FFFFBF;
    exp.limbs[2] = 0x3FFFFFF;
    exp.limbs[3] = 0x3FFFFFF;
    exp.limbs[4] = 0x3FFFFFF;
    exp.limbs[5] = 0x3FFFFFF;
    exp.limbs[6] = 0x3FFFFFF;
    exp.limbs[7] = 0x3FFFFFF;
    exp.limbs[8] = 0x3FFFFFF;
    exp.limbs[9] = 0x00003FF;

    mod_exp_26(r, a, &exp);
}

// =========================================================================
// Elliptic Curve Point Operations (Jacobian Coordinates)
// =========================================================================

typedef struct {
    uint256_26 X;
    uint256_26 Y;
    uint256_26 Z;
    bool isZero;
} ECPoint_Jac_26;

typedef struct {
    uint256_26 x;
    uint256_26 y;
    bool isZero;
} ECPoint_Aff_26;

// Set point to zero (point at infinity)
inline void ec_set_zero_jac_26(thread ECPoint_Jac_26 *p) {
    set_zero_26(&p->X);
    set_zero_26(&p->Y);
    set_int_26(&p->Z, 1);
    p->isZero = true;
}

// Convert affine to Jacobian
inline void ec_affine_to_jac_26(thread ECPoint_Jac_26 *jac, thread const ECPoint_Aff_26 *aff) {
    if (aff->isZero) {
        ec_set_zero_jac_26(jac);
    } else {
        copy_26(&jac->X, &aff->x);
        copy_26(&jac->Y, &aff->y);
        set_int_26(&jac->Z, 1);
        jac->isZero = false;
    }
}

// Point doubling in Jacobian: R = 2*P
inline void ec_double_jac_26(thread ECPoint_Jac_26 *r, thread const ECPoint_Jac_26 *p) {
    if (p->isZero) {
        ec_set_zero_jac_26(r);
        return;
    }

    uint256_26 Y2, S, M, X2, Y4, T, newX, newY, newZ;

    mod_sqr_26(&Y2, &p->Y);
    mod_mult_26(&T, &p->X, &Y2);
    mod_add_26(&S, &T, &T);
    mod_add_26(&S, &S, &S);

    mod_sqr_26(&X2, &p->X);
    mod_add_26(&M, &X2, &X2);
    mod_add_26(&M, &M, &X2);

    mod_sqr_26(&newX, &M);
    mod_sub_26(&newX, &newX, &S);
    mod_sub_26(&newX, &newX, &S);

    mod_sub_26(&T, &S, &newX);
    mod_mult_26(&newY, &M, &T);
    mod_sqr_26(&Y4, &Y2);
    mod_add_26(&T, &Y4, &Y4);
    mod_add_26(&T, &T, &T);
    mod_add_26(&T, &T, &T);
    mod_sub_26(&newY, &newY, &T);

    mod_mult_26(&newZ, &p->Y, &p->Z);
    mod_add_26(&newZ, &newZ, &newZ);

    copy_26(&r->X, &newX);
    copy_26(&r->Y, &newY);
    copy_26(&r->Z, &newZ);
    r->isZero = false;
}

// Load affine point from GTable
inline void gtable_load_point_26(thread ECPoint_Aff_26 *p, device const uint8_t *gTableX, device const uint8_t *gTableY, uint32_t index) {
    load_26_device(&p->x, gTableX + index * 32);
    load_26_device(&p->y, gTableY + index * 32);
    p->isZero = false;
}

// Mixed addition (simpler version without full formula)
inline void ec_add_mixed_jac_26_simple(thread ECPoint_Jac_26 *r, thread const ECPoint_Jac_26 *p, thread const ECPoint_Aff_26 *q) {
    if (p->isZero) {
        ec_affine_to_jac_26(r, q);
        return;
    }
    if (q->isZero) {
        copy_26(&r->X, &p->X);
        copy_26(&r->Y, &p->Y);
        copy_26(&r->Z, &p->Z);
        r->isZero = false;
        return;
    }

    // For now use a simplified formula
    // TODO: Implement full optimized madd-2007-bl formula
    uint256_26 Z2, U2, S2, H, r_val, newX, newY, newZ, T;

    // Z2 = Z^2
    mod_sqr_26(&Z2, &p->Z);

    // U2 = q.x * Z^2
    mod_mult_26(&U2, &q->x, &Z2);

    // S2 = q.y * Z^3
    mod_mult_26(&T, &p->Z, &Z2);
    mod_mult_26(&S2, &q->y, &T);

    // H = U2 - X
    mod_sub_26(&H, &U2, &p->X);

    // r = S2 - Y
    mod_sub_26(&r_val, &S2, &p->Y);

    // X3 = r^2 - H^3 - 2*X*H^2
    uint256_26 HH, HHH, XHH;
    mod_sqr_26(&HH, &H);
    mod_mult_26(&HHH, &HH, &H);
    mod_mult_26(&XHH, &p->X, &HH);

    mod_sqr_26(&newX, &r_val);
    mod_sub_26(&newX, &newX, &HHH);
    mod_sub_26(&newX, &newX, &XHH);
    mod_sub_26(&newX, &newX, &XHH);

    // Y3 = r*(X*H^2 - X3) - Y*H^3
    mod_sub_26(&T, &XHH, &newX);
    mod_mult_26(&newY, &r_val, &T);
    mod_mult_26(&T, &p->Y, &HHH);
    mod_sub_26(&newY, &newY, &T);

    // Z3 = Z*H
    mod_mult_26(&newZ, &p->Z, &H);

    copy_26(&r->X, &newX);
    copy_26(&r->Y, &newY);
    copy_26(&r->Z, &newZ);
    r->isZero = false;
}

// =========================================================================
// GTable Scalar Multiplication (privkey * G)
// =========================================================================

#define NUM_GTABLE_CHUNK 16
#define NUM_GTABLE_VALUE 65536

// Multiply generator point G by scalar using GTable (all in Jacobian coords)
inline void gtable_mult_g_jac_26(
    thread ECPoint_Jac_26 *result,
    thread const uint8_t *privkey,
    device const uint8_t *gTableX,
    device const uint8_t *gTableY
) {
    // Interpret privkey as 16 chunks of 16 bits each
    thread const uint16_t *chunks = (thread const uint16_t *)privkey;

    // Find first non-zero chunk
    int first_chunk = -1;
    for (int i = 0; i < NUM_GTABLE_CHUNK; i++) {
        if (chunks[i] > 0) {
            first_chunk = i;
            break;
        }
    }

    if (first_chunk == -1) {
        // All zero - return point at infinity
        ec_set_zero_jac_26(result);
        return;
    }

    // Load first non-zero point
    uint32_t gtable_index = (first_chunk * NUM_GTABLE_VALUE) + (chunks[first_chunk] - 1);
    ECPoint_Aff_26 point_aff;
    gtable_load_point_26(&point_aff, gTableX, gTableY, gtable_index);

    // Start with first point in Jacobian
    ec_affine_to_jac_26(result, &point_aff);

    // Add remaining chunks
    for (int chunk = first_chunk + 1; chunk < NUM_GTABLE_CHUNK; chunk++) {
        if (chunks[chunk] > 0) {
            gtable_index = (chunk * NUM_GTABLE_VALUE) + (chunks[chunk] - 1);
            gtable_load_point_26(&point_aff, gTableX, gTableY, gtable_index);

            ECPoint_Jac_26 temp;
            ec_add_mixed_jac_26_simple(&temp, result, &point_aff);
            copy_26(&result->X, &temp.X);
            copy_26(&result->Y, &temp.Y);
            copy_26(&result->Z, &temp.Z);
            result->isZero = temp.isZero;
        }
    }
}

// =========================================================================
// Jacobian to Affine Conversion
// =========================================================================

// Convert Jacobian to Affine: (X, Y, Z) -> (X/Z^2, Y/Z^3)
inline void ec_jac_to_affine_26(thread ECPoint_Aff_26 *aff, thread const ECPoint_Jac_26 *jac) {
    if (jac->isZero) {
        set_zero_26(&aff->x);
        set_zero_26(&aff->y);
        aff->isZero = true;
        return;
    }

    // Compute Z^(-1), Z^(-2), Z^(-3)
    uint256_26 Z_inv, Z_inv2, Z_inv3;

    mod_inv_26(&Z_inv, &jac->Z);      // Z^(-1)
    mod_sqr_26(&Z_inv2, &Z_inv);  // Z^(-2) = (Z^-1)^2
    mod_mult_26(&Z_inv3, &Z_inv2, &Z_inv); // Z^(-3)

    // x = X * Z^(-2)
    mod_mult_26(&aff->x, &jac->X, &Z_inv2);

    // y = Y * Z^(-3)
    mod_mult_26(&aff->y, &jac->Y, &Z_inv3);

    aff->isZero = false;
}

#endif // METAL_MATH_26BIT_H
