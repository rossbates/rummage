/*
 * Rummage - Nostr Utilities (Bech32 encoding/decoding)
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

#ifndef NOSTRUTILS_H
#define NOSTRUTILS_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <string>

// Bech32 charset
static const char BECH32_CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

// Bech32 encoding polymod
static inline uint32_t bech32_polymod_step(uint32_t pre) {
    uint8_t b = pre >> 25;
    return ((pre & 0x1FFFFFF) << 5) ^
        (-((b >> 0) & 1) & 0x3b6a57b2UL) ^
        (-((b >> 1) & 1) & 0x26508e6dUL) ^
        (-((b >> 2) & 1) & 0x1ea119faUL) ^
        (-((b >> 3) & 1) & 0x3d4233ddUL) ^
        (-((b >> 4) & 1) & 0x2a1462b3UL);
}

// Convert 8-bit data to 5-bit groups
static inline void convert_bits(const uint8_t *in, size_t inlen, uint8_t *out, size_t *outlen, int frombits, int tobits, bool pad) {
    uint32_t acc = 0;
    int bits = 0;
    size_t idx = 0;
    uint32_t maxv = (1 << tobits) - 1;

    for (size_t i = 0; i < inlen; i++) {
        uint8_t value = in[i];
        acc = (acc << frombits) | value;
        bits += frombits;
        while (bits >= tobits) {
            bits -= tobits;
            out[idx++] = (acc >> bits) & maxv;
        }
    }

    if (pad) {
        if (bits > 0) {
            out[idx++] = (acc << (tobits - bits)) & maxv;
        }
    }

    *outlen = idx;
}

// Encode data to bech32 format
static inline std::string bech32_encode(const char *hrp, const uint8_t *data, size_t data_len) {
    uint8_t data5bit[64];
    size_t data5bit_len;

    // Convert 8-bit to 5-bit
    convert_bits(data, data_len, data5bit, &data5bit_len, 8, 5, true);

    // Calculate checksum
    uint32_t chk = 1;
    size_t hrp_len = strlen(hrp);

    for (size_t i = 0; i < hrp_len; i++) {
        chk = bech32_polymod_step(chk) ^ (hrp[i] >> 5);
    }
    chk = bech32_polymod_step(chk);
    for (size_t i = 0; i < hrp_len; i++) {
        chk = bech32_polymod_step(chk) ^ (hrp[i] & 0x1f);
    }
    for (size_t i = 0; i < data5bit_len; i++) {
        chk = bech32_polymod_step(chk) ^ data5bit[i];
    }
    for (size_t i = 0; i < 6; i++) {
        chk = bech32_polymod_step(chk);
    }
    chk ^= 1;

    // Build output string
    std::string result = hrp;
    result += '1';
    for (size_t i = 0; i < data5bit_len; i++) {
        result += BECH32_CHARSET[data5bit[i]];
    }
    for (int i = 0; i < 6; i++) {
        result += BECH32_CHARSET[(chk >> (5 * (5 - i))) & 0x1f];
    }

    return result;
}

// Convert hex public key to npub
static inline std::string pubkey_to_npub(const uint8_t *pubkey) {
    return bech32_encode("npub", pubkey, 32);
}

// Convert hex private key to nsec
static inline std::string privkey_to_nsec(const uint8_t *privkey) {
    return bech32_encode("nsec", privkey, 32);
}

// Print key pair in various formats
static inline void print_nostr_keypair(const uint8_t *privkey, const uint8_t *pubkey) {
    printf("\n========== NOSTR KEY PAIR ==========\n");

    printf("Private Key (hex):  ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", privkey[i]);
    }
    printf("\n");

    std::string nsec = privkey_to_nsec(privkey);
    printf("Private Key (nsec): %s\n", nsec.c_str());

    printf("Public Key (hex):   ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", pubkey[i]);
    }
    printf("\n");

    std::string npub = pubkey_to_npub(pubkey);
    printf("Public Key (npub):  %s\n", npub.c_str());

    printf("====================================\n\n");
}

#endif // NOSTRUTILS_H
