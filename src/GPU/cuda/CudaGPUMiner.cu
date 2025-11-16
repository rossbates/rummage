/*
 * Rummage - GPU Nostr Vanity Key Miner - CUDA Kernel
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

#include "CudaGPUMiner.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <curand_kernel.h>
#include <sys/stat.h>

#include "CudaMath.h"

using namespace std;

inline void __cudaSafeCall(cudaError err, const char *file, const int line)
{
    if (cudaSuccess != err)
    {
        printf("cudaSafeCall() failed at %s:%i : %s\n", file, line, cudaGetErrorString(err));
        fprintf(stderr, "cudaSafeCall() failed at %s:%i : %s\n", file, line, cudaGetErrorString(err));
        exit(-1);
    }
}

// Bech32 charset (32 characters + null terminator)
__constant__ char BECH32_CHARSET[33] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

// Bech32 HRP expansion for "npub"
__constant__ uint32_t BECH32_HRP_EXPAND[5] = {3, 3, 3, 3, 16}; // npub expansion

// 256-bit starting offset for sequential search mode
__constant__ uint8_t d_startOffset[32];

// Convert hex character to integer
__device__ uint8_t hexCharToInt(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Convert byte to hex characters
__device__ void byteToHex(uint8_t byte, char *hex) {
    const char hexChars[] = "0123456789abcdef";
    hex[0] = hexChars[(byte >> 4) & 0xF];
    hex[1] = hexChars[byte & 0xF];
}

// Bech32 polymod function for checksum
__device__ uint32_t bech32_polymod(uint8_t *values, int len) {
    uint32_t chk = 1;
    uint32_t GEN[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};

    for (int i = 0; i < len; i++) {
        uint8_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ values[i];
        for (int j = 0; j < 5; j++) {
            if ((top >> j) & 1) {
                chk ^= GEN[j];
            }
        }
    }
    return chk;
}

// Convert 8-bit array to 5-bit array for bech32
__device__ void convert_bits(uint8_t *out, int *outlen, uint8_t *in, int inlen, int frombits, int tobits, bool pad) {
    uint32_t acc = 0;
    int bits = 0;
    int maxv = (1 << tobits) - 1;
    int max_acc = (1 << (frombits + tobits - 1)) - 1;
    *outlen = 0;

    for (int i = 0; i < inlen; i++) {
        acc = ((acc << frombits) | in[i]) & max_acc;
        bits += frombits;
        while (bits >= tobits) {
            bits -= tobits;
            out[(*outlen)++] = (acc >> bits) & maxv;
        }
    }

    if (pad) {
        if (bits > 0) {
            out[(*outlen)++] = (acc << (tobits - bits)) & maxv;
        }
    }
}

// Encode pubkey to bech32 npub format
// Returns the npub string without "npub1" prefix (just the encoded part)
__device__ void encode_npub(uint8_t *pubkey_32bytes, char *npub_out) {
    // Convert pubkey to 5-bit groups
    uint8_t data5[52]; // 256 bits / 5 bits per group = 51.2, rounded up = 52
    int data5_len;
    convert_bits(data5, &data5_len, pubkey_32bytes, 32, 8, 5, true);

    // Create values array for checksum: HRP expansion + data + 6 zeros
    uint8_t values[63]; // 5 (HRP) + 52 (data) + 6 (checksum placeholder)

    // Add HRP expansion for "npub"
    values[0] = 3;  // 'n' >> 5
    values[1] = 3;  // 'p' >> 5
    values[2] = 3;  // 'u' >> 5
    values[3] = 3;  // 'b' >> 5
    values[4] = 16; // separator

    // Add data
    for (int i = 0; i < data5_len; i++) {
        values[5 + i] = data5[i];
    }

    // Add 6 zeros for checksum calculation
    for (int i = 0; i < 6; i++) {
        values[5 + data5_len + i] = 0;
    }

    // Calculate checksum
    uint32_t polymod = bech32_polymod(values, 5 + data5_len + 6) ^ 1;

    // Extract checksum (6 characters)
    uint8_t checksum[6];
    for (int i = 0; i < 6; i++) {
        checksum[i] = (polymod >> (5 * (5 - i))) & 31;
    }

    // Encode data to bech32 charset
    for (int i = 0; i < data5_len; i++) {
        npub_out[i] = BECH32_CHARSET[data5[i]];
    }

    // Append checksum
    for (int i = 0; i < 6; i++) {
        npub_out[data5_len + i] = BECH32_CHARSET[checksum[i]];
    }

    // Null terminate
    npub_out[data5_len + 6] = '\0';
}

// Check if bech32 string matches pattern
__device__ bool matchesBech32Pattern(char *npub, uint8_t *pattern, uint8_t patternLen, bool isPrefix) {
    if (isPrefix) {
        // Check prefix match (skip "npub1" part, match against encoded data)
        for (uint8_t i = 0; i < patternLen; i++) {
            if (npub[i] != pattern[i]) return false;
        }
    } else {
        // Check suffix match (before checksum - last 6 chars are checksum)
        int data_len = 52;  // Length without checksum
        int start_pos = data_len - patternLen;

        for (uint8_t i = 0; i < patternLen; i++) {
            if (npub[start_pos + i] != pattern[i]) return false;
        }
    }
    return true;
}

// Check if public key matches hex vanity pattern
__device__ bool matchesHexPattern(uint64_t *pubkey, uint8_t *pattern, uint8_t patternLen, bool isPrefix) {
    uint8_t *pubkeyBytes = (uint8_t *)pubkey;
    char hex[2];

    if (isPrefix) {
        // Check prefix match
        for (uint8_t i = 0; i < patternLen; i++) {
            byteToHex(pubkeyBytes[i / 2], hex);
            if (i % 2 == 0) {
                if (hex[0] != pattern[i]) return false;
            } else {
                if (hex[1] != pattern[i]) return false;
            }
        }
    } else {
        // Check suffix match
        int pubkeyByteLen = 32; // x-only pubkey is 32 bytes = 64 hex chars
        int startByte = pubkeyByteLen - ((patternLen + 1) / 2);
        int startChar = (patternLen % 2 == 1) ? 1 : 0;

        for (uint8_t i = 0; i < patternLen; i++) {
            int byteIdx = startByte + (i + startChar) / 2;
            byteToHex(pubkeyBytes[byteIdx], hex);
            if ((i + startChar) % 2 == 0) {
                if (hex[0] != pattern[i]) return false;
            } else {
                if (hex[1] != pattern[i]) return false;
            }
        }
    }

    return true;
}

//Cuda Secp256k1 Point Multiplication (from GPUSecp.cu)
//Takes 32-byte privKey + gTable and outputs 64-byte public key [qx,qy]
__device__ void _PointMultiSecp256k1Nostr(uint64_t *qx, uint64_t *qy, uint16_t *privKey, uint8_t *gTableX, uint8_t *gTableY) {
    int chunk = 0;
    uint64_t qz[5] = {1, 0, 0, 0, 0};

    //Find the first non-zero point [qx,qy]
    for (; chunk < NUM_GTABLE_CHUNK; chunk++) {
        if (privKey[chunk] > 0) {
            int index = (NOSTR_CHUNK_FIRST_ELEMENT[chunk] + (privKey[chunk] - 1)) * SIZE_GTABLE_POINT;
            memcpy(qx, gTableX + index, SIZE_GTABLE_POINT);
            memcpy(qy, gTableY + index, SIZE_GTABLE_POINT);
            chunk++;
            break;
        }
    }

    //Add the remaining chunks together
    for (; chunk < NUM_GTABLE_CHUNK; chunk++) {
        if (privKey[chunk] > 0) {
            uint64_t gx[4];
            uint64_t gy[4];

            int index = (NOSTR_CHUNK_FIRST_ELEMENT[chunk] + (privKey[chunk] - 1)) * SIZE_GTABLE_POINT;

            memcpy(gx, gTableX + index, SIZE_GTABLE_POINT);
            memcpy(gy, gTableY + index, SIZE_GTABLE_POINT);

            _PointAddSecp256k1(qx, qy, qz, gx, gy);
        }
    }

    //Performing modular inverse on qz to obtain the public key [qx,qy]
    _ModInv(qz);
    _ModMult(qx, qz);
    _ModMult(qy, qz);
}

//GPU kernel to initialize cuRAND states (called once at startup)
__global__ void CudaInitRandStates(curandState *randStates, uint64_t seed) {
    int idxThread = NOSTR_IDX_CUDA_THREAD;

    // Initialize this thread's RNG state with unique seed
    curand_init(seed + idxThread, 0, 0, &randStates[idxThread]);
}

//GPU kernel function for Nostr vanity key mining (BATCHED)
//GPU kernel for Sequential vanity key mining (exhaustive search)
__global__ void CudaNostrVanityMineSequential(
    uint64_t globalIteration,  // Current global iteration
    uint8_t *gTableXGPU,
    uint8_t *gTableYGPU,
    uint8_t *vanityPatternGPU,
    uint8_t vanityLen,
    int vanityMode,
    uint8_t *outputFoundGPU,
    uint8_t *outputPrivKeysGPU,
    uint8_t *outputPubKeysGPU)
{
    int idxThread = NOSTR_IDX_CUDA_THREAD;

    // Calculate sequential key index for this thread in this batch
    // Each thread handles KEYS_PER_THREAD_BATCH keys per global iteration
    uint64_t baseKeyIndex = globalIteration * NOSTR_COUNT_CUDA_THREADS * KEYS_PER_THREAD_BATCH;
    uint64_t threadBaseIndex = baseKeyIndex + (idxThread * KEYS_PER_THREAD_BATCH);

    // Generate and check MULTIPLE keys sequentially
    for (int batch = 0; batch < KEYS_PER_THREAD_BATCH; batch++) {
        uint64_t keyIndex = threadBaseIndex + batch;

        // Start with the base offset from constant memory
        uint8_t privKey[SIZE_PRIV_KEY_NOSTR];
        for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
            privKey[i] = d_startOffset[i];
        }

        // Add keyIndex to the offset (256-bit addition with carry)
        uint64_t carry = keyIndex;
        for (int i = SIZE_PRIV_KEY_NOSTR - 1; i >= 0 && carry > 0; i--) {
            uint64_t sum = privKey[i] + (carry & 0xFF);
            privKey[i] = sum & 0xFF;
            carry = (carry >> 8) + (sum >> 8);
        }

        // Ensure private key is valid (not zero)
        bool isZero = true;
        for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
            if (privKey[i] != 0) {
                isZero = false;
                break;
            }
        }
        if (isZero) continue;  // Skip zero key

        // Compute secp256k1 public key
        uint64_t qx[4];
        uint64_t qy[4];
        _PointMultiSecp256k1Nostr(qx, qy, (uint16_t *)privKey, gTableXGPU, gTableYGPU);

        // Check if x-coordinate matches vanity pattern (same logic as random mode)
        bool matched = false;

        if (vanityMode == 0) { // VANITY_HEX_PREFIX
            matched = matchesHexPattern(qx, vanityPatternGPU, vanityLen, true);
        } else if (vanityMode == 1) { // VANITY_HEX_SUFFIX
            matched = matchesHexPattern(qx, vanityPatternGPU, vanityLen, false);
        } else if (vanityMode == 2) { // VANITY_HEX_BOTH
            uint8_t halfLen = vanityLen / 2;
            matched = matchesHexPattern(qx, vanityPatternGPU, halfLen, true) &&
                      matchesHexPattern(qx, vanityPatternGPU + halfLen, vanityLen - halfLen, false);
        } else if (vanityMode == 3 || vanityMode == 4 || vanityMode == 5) {
            // VANITY_BECH32_PREFIX, VANITY_BECH32_SUFFIX, VANITY_BECH32_BOTH
            uint8_t *qxBytes = (uint8_t *)qx;
            char npub[64];
            encode_npub(qxBytes, npub);

            if (vanityMode == 3) {
                matched = matchesBech32Pattern(npub, vanityPatternGPU, vanityLen, true);
            } else if (vanityMode == 4) {
                matched = matchesBech32Pattern(npub, vanityPatternGPU, vanityLen, false);
            } else if (vanityMode == 5) {
                uint8_t halfLen = vanityLen / 2;
                matched = matchesBech32Pattern(npub, vanityPatternGPU, halfLen, true) &&
                          matchesBech32Pattern(npub, vanityPatternGPU + halfLen, vanityLen - halfLen, false);
            }
        }

        // If matched and we haven't already found one, store it
        if (matched && outputFoundGPU[idxThread] == 0) {
            outputFoundGPU[idxThread] = 1;

            // Copy private key to output
            for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
                outputPrivKeysGPU[(idxThread * SIZE_PRIV_KEY_NOSTR) + i] = privKey[i];
            }

            // Copy public key (x-only) to output
            uint8_t *qxBytes = (uint8_t *)qx;
            for (int i = 0; i < SIZE_PUBKEY_NOSTR; i++) {
                outputPubKeysGPU[(idxThread * SIZE_PUBKEY_NOSTR) + i] = qxBytes[i];
            }

            break;  // Found a match, stop checking this batch
        }
    }
}

//GPU kernel for Random vanity key mining (original random search)
__global__ void CudaNostrVanityMine(
    curandState *randStates,
    uint8_t *gTableXGPU,
    uint8_t *gTableYGPU,
    uint8_t *vanityPatternGPU,
    uint8_t vanityLen,
    int vanityMode,
    uint8_t *outputFoundGPU,
    uint8_t *outputPrivKeysGPU,
    uint8_t *outputPubKeysGPU)
{
    int idxThread = NOSTR_IDX_CUDA_THREAD;

    // Use pre-initialized RNG state
    curandState localState = randStates[idxThread];

    // Generate and check MULTIPLE keys per thread (BATCHING!)
    for (int batch = 0; batch < KEYS_PER_THREAD_BATCH; batch++) {
        // Generate random 32-byte private key
        uint8_t privKey[SIZE_PRIV_KEY_NOSTR];
        uint32_t *privKey32 = (uint32_t *)privKey;

        for (int i = 0; i < 8; i++) {
            privKey32[i] = curand(&localState);
        }

        // Ensure private key is valid (not zero)
        bool isZero = true;
        for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
            if (privKey[i] != 0) {
                isZero = false;
                break;
            }
        }

        if (isZero) {
            privKey[31] = 1;
        }

        // Compute secp256k1 public key
        uint64_t qx[4];
        uint64_t qy[4];

        _PointMultiSecp256k1Nostr(qx, qy, (uint16_t *)privKey, gTableXGPU, gTableYGPU);

        // Check if x-coordinate matches vanity pattern
        bool matched = false;

        if (vanityMode == 0) { // VANITY_HEX_PREFIX
            matched = matchesHexPattern(qx, vanityPatternGPU, vanityLen, true);
        } else if (vanityMode == 1) { // VANITY_HEX_SUFFIX
            matched = matchesHexPattern(qx, vanityPatternGPU, vanityLen, false);
        } else if (vanityMode == 2) { // VANITY_HEX_BOTH
            uint8_t halfLen = vanityLen / 2;
            matched = matchesHexPattern(qx, vanityPatternGPU, halfLen, true) &&
                      matchesHexPattern(qx, vanityPatternGPU + halfLen, vanityLen - halfLen, false);
        } else if (vanityMode == 3 || vanityMode == 4 || vanityMode == 5) {
            // VANITY_BECH32_PREFIX, VANITY_BECH32_SUFFIX, VANITY_BECH32_BOTH

            // Encode public key to bech32
            uint8_t *qxBytes = (uint8_t *)qx;
            char npub[64]; // 52 data + 6 checksum + null terminator
            encode_npub(qxBytes, npub);

            // Check pattern match
            if (vanityMode == 3) { // VANITY_BECH32_PREFIX
                matched = matchesBech32Pattern(npub, vanityPatternGPU, vanityLen, true);
            } else if (vanityMode == 4) { // VANITY_BECH32_SUFFIX
                matched = matchesBech32Pattern(npub, vanityPatternGPU, vanityLen, false);
            } else if (vanityMode == 5) { // VANITY_BECH32_BOTH
                uint8_t halfLen = vanityLen / 2;
                matched = matchesBech32Pattern(npub, vanityPatternGPU, halfLen, true) &&
                          matchesBech32Pattern(npub, vanityPatternGPU + halfLen, vanityLen - halfLen, false);
            }
        }

        // If matched and we haven't already found one, store it
        if (matched && outputFoundGPU[idxThread] == 0) {
            // Mark that we found a match
            outputFoundGPU[idxThread] = 1;

            // Copy private key to output
            for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
                outputPrivKeysGPU[(idxThread * SIZE_PRIV_KEY_NOSTR) + i] = privKey[i];
            }

            // Copy public key (x-only) to output
            uint8_t *qxBytes = (uint8_t *)qx;
            for (int i = 0; i < SIZE_PUBKEY_NOSTR; i++) {
                outputPubKeysGPU[(idxThread * SIZE_PUBKEY_NOSTR) + i] = qxBytes[i];
            }

            // Break after first match to save time
            break;
        }
    } // End of batch loop

    // Save updated RNG state back to global memory
    randStates[idxThread] = localState;
}

//Constructor
CudaGPUMiner::CudaGPUMiner(
    const uint8_t *gTableXCPU,
    const uint8_t *gTableYCPU,
    const char *vanityPattern,
    VanityMode mode,
    const uint8_t *startOffsetParam,
    SearchMode searchMode,
    int bech32PatternLen)
{
    printf("CudaGPUMiner Starting\n");

    this->vanityMode = mode;
    this->vanityLen = strlen(vanityPattern);
    this->keysGenerated = 0;
    this->matchesFound = 0;
    memcpy(this->startOffset, startOffsetParam, 32);
    this->searchMode = searchMode;
    this->currentIteration = 0;
    this->needsBech32Verification = false;  // Will be set by main program if needed

    // Calculate search space size and total iterations for sequential mode
    if (searchMode == SEARCH_SEQUENTIAL) {
        if (bech32PatternLen > 0) {
            // For bech32 patterns: each char = 5 bits
            // Use original bech32 pattern length for accurate search space
            this->searchSpaceSize = 1ULL << (bech32PatternLen * 5);
            printf("Sequential mode enabled (bech32 pattern)\n");
            printf("Original bech32 pattern: %d characters = %d bits\n", bech32PatternLen, bech32PatternLen * 5);
        } else {
            // For hex patterns: each char = 4 bits
            this->searchSpaceSize = 1ULL << (vanityLen * 4);
            printf("Sequential mode enabled (hex pattern)\n");
        }
        this->totalIterations = (searchSpaceSize + (NOSTR_COUNT_CUDA_THREADS * KEYS_PER_THREAD_BATCH) - 1) /
                                (NOSTR_COUNT_CUDA_THREADS * KEYS_PER_THREAD_BATCH);
        printf("Search space size: %lu keys (2^%d)\n", searchSpaceSize,
               bech32PatternLen > 0 ? bech32PatternLen * 5 : vanityLen * 4);
        printf("Total iterations needed: %lu\n", totalIterations);
    } else {
        this->searchSpaceSize = 0;
        this->totalIterations = 0;
    }

    int gpuId = 0; // FOR MULTIPLE GPUS EDIT THIS
    CudaSafeCall(cudaSetDevice(gpuId));

    cudaDeviceProp deviceProp;
    CudaSafeCall(cudaGetDeviceProperties(&deviceProp, gpuId));

    printf("GPU.gpuId: #%d \n", gpuId);
    printf("GPU.deviceProp.name: %s \n", deviceProp.name);
    printf("GPU.multiProcessorCount: %d \n", deviceProp.multiProcessorCount);
    printf("GPU.BLOCKS_PER_GRID: %d \n", NOSTR_BLOCKS_PER_GRID);
    printf("GPU.THREADS_PER_BLOCK: %d \n", NOSTR_THREADS_PER_BLOCK);
    printf("GPU.CUDA_THREAD_COUNT: %d \n", NOSTR_COUNT_CUDA_THREADS);
    printf("GPU.vanityPattern: %s \n", vanityPattern);
    printf("GPU.vanityMode: %d \n", mode);
    printf("GPU.vanityLen: %d \n", vanityLen);

    printf("Allocating gTableX \n");
    CudaSafeCall(cudaMalloc((void **)&gTableXGPU, COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT));
    CudaSafeCall(cudaMemcpy(gTableXGPU, gTableXCPU, COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT, cudaMemcpyHostToDevice));

    printf("Allocating gTableY \n");
    CudaSafeCall(cudaMalloc((void **)&gTableYGPU, COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT));
    CudaSafeCall(cudaMemcpy(gTableYGPU, gTableYCPU, COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT, cudaMemcpyHostToDevice));

    printf("Allocating vanity pattern buffer \n");
    int maxPatternLen = (mode >= VANITY_BECH32_PREFIX) ? MAX_VANITY_BECH32_LEN : MAX_VANITY_HEX_LEN;
    CudaSafeCall(cudaMalloc((void **)&vanityPatternGPU, maxPatternLen));
    CudaSafeCall(cudaMemcpy(vanityPatternGPU, vanityPattern, vanityLen, cudaMemcpyHostToDevice));

    printf("Allocating outputFound buffer \n");
    CudaSafeCall(cudaMalloc((void **)&outputFoundGPU, NOSTR_COUNT_CUDA_THREADS));
    CudaSafeCall(cudaHostAlloc(&outputFoundCPU, NOSTR_COUNT_CUDA_THREADS, cudaHostAllocWriteCombined | cudaHostAllocMapped));

    printf("Allocating outputPrivKeys buffer \n");
    CudaSafeCall(cudaMalloc((void **)&outputPrivKeysGPU, NOSTR_COUNT_CUDA_THREADS * SIZE_PRIV_KEY_NOSTR));
    CudaSafeCall(cudaHostAlloc(&outputPrivKeysCPU, NOSTR_COUNT_CUDA_THREADS * SIZE_PRIV_KEY_NOSTR, cudaHostAllocWriteCombined | cudaHostAllocMapped));

    printf("Allocating outputPubKeys buffer \n");
    CudaSafeCall(cudaMalloc((void **)&outputPubKeysGPU, NOSTR_COUNT_CUDA_THREADS * SIZE_PUBKEY_NOSTR));
    CudaSafeCall(cudaHostAlloc(&outputPubKeysCPU, NOSTR_COUNT_CUDA_THREADS * SIZE_PUBKEY_NOSTR, cudaHostAllocWriteCombined | cudaHostAllocMapped));

    // Only allocate cuRAND for random mode
    if (searchMode == SEARCH_RANDOM) {
        printf("Allocating cuRAND states buffer \n");
        CudaSafeCall(cudaMalloc((void **)&randStatesGPU, NOSTR_COUNT_CUDA_THREADS * sizeof(curandState)));

        printf("Initializing cuRAND states (this may take a moment)...\n");
        // Use last 8 bytes of startOffset as seed for random mode
        uint64_t seed;
        memcpy(&seed, startOffset + 24, 8);
        CudaInitRandStates<<<NOSTR_BLOCKS_PER_GRID, NOSTR_THREADS_PER_BLOCK>>>(randStatesGPU, seed);
        CudaSafeCall(cudaDeviceSynchronize());
        printf("cuRAND initialization complete!\n");
    } else {
        printf("Sequential mode: Skipping cuRAND initialization\n");
        // Copy startOffset to GPU constant memory for sequential mode
        CudaSafeCall(cudaMemcpyToSymbol(d_startOffset, this->startOffset, 32));
        printf("Starting offset copied to GPU constant memory\n");
        randStatesGPU = nullptr;
    }

    printf("Allocation Complete \n");
    CudaSafeCall(cudaGetLastError());
}

void CudaGPUMiner::doIteration(uint64_t iteration) {
    // Clear output buffers
    CudaSafeCall(cudaMemset(outputFoundGPU, 0, NOSTR_COUNT_CUDA_THREADS));
    CudaSafeCall(cudaMemset(outputPrivKeysGPU, 0, NOSTR_COUNT_CUDA_THREADS * SIZE_PRIV_KEY_NOSTR));
    CudaSafeCall(cudaMemset(outputPubKeysGPU, 0, NOSTR_COUNT_CUDA_THREADS * SIZE_PUBKEY_NOSTR));

    // Launch appropriate kernel based on search mode
    if (searchMode == SEARCH_SEQUENTIAL) {
        CudaNostrVanityMineSequential<<<NOSTR_BLOCKS_PER_GRID, NOSTR_THREADS_PER_BLOCK>>>(
            currentIteration,
            gTableXGPU,
            gTableYGPU,
            vanityPatternGPU,
            vanityLen,
            (int)vanityMode,
            outputFoundGPU,
            outputPrivKeysGPU,
            outputPubKeysGPU);
        currentIteration++;
    } else {
        // Random mode
        CudaNostrVanityMine<<<NOSTR_BLOCKS_PER_GRID, NOSTR_THREADS_PER_BLOCK>>>(
            randStatesGPU,
            gTableXGPU,
            gTableYGPU,
            vanityPatternGPU,
            vanityLen,
            (int)vanityMode,
            outputFoundGPU,
            outputPrivKeysGPU,
            outputPubKeysGPU);
    }

    // Copy results back to CPU
    CudaSafeCall(cudaMemcpy(outputFoundCPU, outputFoundGPU, NOSTR_COUNT_CUDA_THREADS, cudaMemcpyDeviceToHost));
    CudaSafeCall(cudaMemcpy(outputPrivKeysCPU, outputPrivKeysGPU, NOSTR_COUNT_CUDA_THREADS * SIZE_PRIV_KEY_NOSTR, cudaMemcpyDeviceToHost));
    CudaSafeCall(cudaMemcpy(outputPubKeysCPU, outputPubKeysGPU, NOSTR_COUNT_CUDA_THREADS * SIZE_PUBKEY_NOSTR, cudaMemcpyDeviceToHost));
    CudaSafeCall(cudaGetLastError());

    // Account for batching: each thread checks KEYS_PER_THREAD_BATCH keys
    keysGenerated += NOSTR_COUNT_CUDA_THREADS * KEYS_PER_THREAD_BATCH;
}

// CPU-side bech32 encoder (for display purposes)
void encode_npub_cpu(uint8_t *pubkey_32bytes, char *npub_out) {
    const char *bech32_charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

    // Convert pubkey to 5-bit groups
    uint8_t data5[52];
    int data5_len = 0;
    uint32_t acc = 0;
    int bits = 0;

    for (int i = 0; i < 32; i++) {
        acc = ((acc << 8) | pubkey_32bytes[i]) & 0x1fff;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            data5[data5_len++] = (acc >> bits) & 31;
        }
    }
    if (bits > 0) {
        data5[data5_len++] = (acc << (5 - bits)) & 31;
    }

    // Create values array for checksum
    uint8_t values[63];
    values[0] = 3; values[1] = 3; values[2] = 3; values[3] = 3; values[4] = 16;
    for (int i = 0; i < data5_len; i++) values[5 + i] = data5[i];
    for (int i = 0; i < 6; i++) values[5 + data5_len + i] = 0;

    // Calculate checksum
    uint32_t chk = 1;
    uint32_t GEN[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    for (int i = 0; i < 5 + data5_len + 6; i++) {
        uint8_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ values[i];
        for (int j = 0; j < 5; j++) {
            if ((top >> j) & 1) chk ^= GEN[j];
        }
    }
    chk ^= 1;

    // Extract checksum
    uint8_t checksum[6];
    for (int i = 0; i < 6; i++) checksum[i] = (chk >> (5 * (5 - i))) & 31;

    // Encode to bech32 charset
    for (int i = 0; i < data5_len; i++) npub_out[i] = bech32_charset[data5[i]];
    for (int i = 0; i < 6; i++) npub_out[data5_len + i] = bech32_charset[checksum[i]];
    npub_out[data5_len + 6] = '\0';
}

bool CudaGPUMiner::checkAndPrintResults() {
    bool foundAny = false;

    for (int idxThread = 0; idxThread < NOSTR_COUNT_CUDA_THREADS; idxThread++) {
        if (outputFoundCPU[idxThread] > 0) {
            // Get private and public keys
            uint8_t *privKey = &outputPrivKeysCPU[idxThread * SIZE_PRIV_KEY_NOSTR];
            uint8_t *pubKey = &outputPubKeysCPU[idxThread * SIZE_PUBKEY_NOSTR];

            // If we converted from bech32 to hex, verify the full bech32 pattern
            if (needsBech32Verification) {
                char npub[64];
                encode_npub_cpu(pubKey, npub);

                // Check if full bech32 pattern matches
                bool bech32Match = false;
                size_t patternLen = strlen(originalBech32Pattern);

                if (originalBech32Mode == VANITY_BECH32_PREFIX) {
                    // Check prefix
                    bech32Match = (strncmp(npub, originalBech32Pattern, patternLen) == 0);
                } else if (originalBech32Mode == VANITY_BECH32_SUFFIX) {
                    // Check suffix
                    size_t npubLen = strlen(npub) - 6; // Exclude checksum
                    if (npubLen >= patternLen) {
                        bech32Match = (strncmp(npub + npubLen - patternLen, originalBech32Pattern, patternLen) == 0);
                    }
                } else if (originalBech32Mode == VANITY_BECH32_BOTH) {
                    // Check both prefix and suffix
                    size_t halfLen = patternLen / 2;
                    bool prefixMatch = (strncmp(npub, originalBech32Pattern, halfLen) == 0);
                    size_t npubLen = strlen(npub) - 6;
                    size_t suffixLen = patternLen - halfLen;
                    bool suffixMatch = (npubLen >= suffixLen) &&
                                       (strncmp(npub + npubLen - suffixLen, originalBech32Pattern + halfLen, suffixLen) == 0);
                    bech32Match = prefixMatch && suffixMatch;
                }

                // If bech32 doesn't match, this is a false positive from hex pre-filter
                if (!bech32Match) {
                    continue;  // Skip this result
                }
            }

            foundAny = true;
            matchesFound++;

            printf("\n========== MATCH FOUND ==========\n");
            printf("Private Key (hex): ");
            for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
                printf("%02x", privKey[i]);
            }
            printf("\n");

            printf("Public Key (hex):  ");
            for (int i = 0; i < SIZE_PUBKEY_NOSTR; i++) {
                printf("%02x", pubKey[i]);
            }
            printf("\n");

            // If we verified bech32 or in bech32 mode, also display the npub
            if (needsBech32Verification || vanityMode >= VANITY_BECH32_PREFIX) {
                char npub[64];
                encode_npub_cpu(pubKey, npub);
                printf("Public Key (npub): npub1%s\n", npub);
            }

            printf("Total keys searched: %lu\n", keysGenerated);
            printf("=================================\n\n");

            // Write to file
            FILE *file = fopen("keys.txt", "a");
            if (file != NULL) {
                fprintf(file, "\n========== MATCH FOUND ==========\n");
                fprintf(file, "Private Key (hex): ");
                for (int i = 0; i < SIZE_PRIV_KEY_NOSTR; i++) {
                    fprintf(file, "%02x", privKey[i]);
                }
                fprintf(file, "\n");

                fprintf(file, "Public Key (hex):  ");
                for (int i = 0; i < SIZE_PUBKEY_NOSTR; i++) {
                    fprintf(file, "%02x", pubKey[i]);
                }
                fprintf(file, "\n");

                // If we verified bech32 or in bech32 mode, also write the npub
                if (needsBech32Verification || vanityMode >= VANITY_BECH32_PREFIX) {
                    char npub[64];
                    encode_npub_cpu(pubKey, npub);
                    fprintf(file, "Public Key (npub): npub1%s\n", npub);
                }

                fprintf(file, "Total keys searched: %lu\n", keysGenerated);
                fprintf(file, "=================================\n\n");
                fclose(file);
            }
        }
    }

    return foundAny;
}

void CudaGPUMiner::doFreeMemory() {
    printf("\nCudaGPUMiner Freeing memory... ");

    CudaSafeCall(cudaFree(gTableXGPU));
    CudaSafeCall(cudaFree(gTableYGPU));
    CudaSafeCall(cudaFree(vanityPatternGPU));

    if (randStatesGPU != nullptr) {
        CudaSafeCall(cudaFree(randStatesGPU));
    }

    CudaSafeCall(cudaFreeHost(outputFoundCPU));
    CudaSafeCall(cudaFree(outputFoundGPU));

    CudaSafeCall(cudaFreeHost(outputPrivKeysCPU));
    CudaSafeCall(cudaFree(outputPrivKeysGPU));

    CudaSafeCall(cudaFreeHost(outputPubKeysCPU));
    CudaSafeCall(cudaFree(outputPubKeysGPU));

    printf("Done \n");
}

// Sequential search checkpoint save
bool CudaGPUMiner::saveCheckpoint(const char *filename) {
    if (searchMode != SEARCH_SEQUENTIAL) {
        return false;  // Only for sequential mode
    }

    FILE *file = fopen(filename, "w");
    if (!file) {
        return false;
    }

    fprintf(file, "# Nostr Vanity Miner Sequential Search Checkpoint\n");
    fprintf(file, "# WARNING: This file contains your search offset - protect it like a private key!\n");

    // Save offset as hex string
    fprintf(file, "startOffset=");
    for (int i = 0; i < 32; i++) {
        fprintf(file, "%02x", startOffset[i]);
    }
    fprintf(file, "\n");

    fprintf(file, "currentIteration=%lu\n", currentIteration);
    fprintf(file, "keysGenerated=%lu\n", keysGenerated);
    fprintf(file, "matchesFound=%lu\n", matchesFound);
    fprintf(file, "searchSpaceSize=%lu\n", searchSpaceSize);
    fprintf(file, "totalIterations=%lu\n", totalIterations);

    fclose(file);

    // Set restrictive permissions (owner read/write only)
    chmod(filename, 0600);

    return true;
}

// Sequential search checkpoint load
bool CudaGPUMiner::loadCheckpoint(const char *filename) {
    if (searchMode != SEARCH_SEQUENTIAL) {
        return false;  // Only for sequential mode
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        return false;
    }

    char line[512];
    bool offsetLoaded = false;
    uint8_t loadedOffset[32];

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') continue;  // Skip comments

        // Parse offset
        if (strncmp(line, "startOffset=", 12) == 0) {
            char *hexStr = line + 12;
            for (int i = 0; i < 32; i++) {
                char byteStr[3] = {hexStr[i*2], hexStr[i*2+1], '\0'};
                loadedOffset[i] = (uint8_t)strtol(byteStr, NULL, 16);
            }
            offsetLoaded = true;
            continue;
        }

        if (sscanf(line, "currentIteration=%lu", &currentIteration) == 1) continue;
        if (sscanf(line, "keysGenerated=%lu", &keysGenerated) == 1) continue;
        if (sscanf(line, "matchesFound=%lu", &matchesFound) == 1) continue;
        if (sscanf(line, "searchSpaceSize=%lu", &searchSpaceSize) == 1) continue;
        if (sscanf(line, "totalIterations=%lu", &totalIterations) == 1) continue;
    }

    fclose(file);

    if (!offsetLoaded) {
        fprintf(stderr, "Error: Checkpoint file missing startOffset\n");
        return false;
    }

    // Verify that the loaded offset matches the current offset
    bool offsetMatches = true;
    for (int i = 0; i < 32; i++) {
        if (loadedOffset[i] != startOffset[i]) {
            offsetMatches = false;
            break;
        }
    }

    if (!offsetMatches) {
        fprintf(stderr, "Error: Checkpoint offset does not match current offset\n");
        fprintf(stderr, "This checkpoint is from a different search session\n");
        return false;
    }

    printf("Checkpoint loaded: iteration %lu / %lu (%.2f%% complete)\n",
           currentIteration, totalIterations, getSearchProgress() * 100.0);
    return true;
}

// Get search progress (0.0 to 1.0)
double CudaGPUMiner::getSearchProgress() const {
    if (searchMode != SEARCH_SEQUENTIAL || totalIterations == 0) {
        return 0.0;
    }
    return (double)currentIteration / (double)totalIterations;
}
