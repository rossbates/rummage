/*
 * Rummage - Metal GPU Miner Implementation
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

#import "MetalGPUMiner.h"
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include "../NostrUtils.h"
#include <stdio.h>
#include <string.h>

// CPU-side bech32 encoder (for display purposes)
static void encode_npub_cpu(uint8_t *pubkey_32bytes, char *npub_out) {
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
#include <stdlib.h>
#include <sys/stat.h>

MetalGPUMiner::MetalGPUMiner(
    const uint8_t *gTableXCPU,
    const uint8_t *gTableYCPU,
    const char *vanityPattern,
    VanityMode mode,
    const uint8_t *startOffsetInput,
    SearchMode searchModeInput,
    int bech32PatternLen
) : device(nullptr),
    commandQueue(nullptr),
    library(nullptr),
    randomPipeline(nullptr),
    sequentialPipeline(nullptr),
    gTableXBuffer(nullptr),
    gTableYBuffer(nullptr),
    vanityPatternBuffer(nullptr),
    startOffsetBuffer(nullptr),
    resultsBuffer(nullptr),
    privKeysBuffer(nullptr),
    pubKeysBuffer(nullptr),
    outputFoundCPU(nullptr),
    outputPrivKeysCPU(nullptr),
    outputPubKeysCPU(nullptr),
    vanityLen(0),
    vanityMode(mode),
    searchMode(searchModeInput),
    keysGenerated(0),
    matchesFound(0),
    currentIteration(0),
    totalIterations(0),
    searchSpaceSize(0),
    needsBech32Verification(false)
{
    printf("\nMetalGPUMiner initializing...\n");

    // Store vanity pattern
    vanityLen = strlen(vanityPattern);
    memcpy(this->startOffset, startOffsetInput, 32);

    // Initialize Metal
    if (!initializeMetal()) {
        fprintf(stderr, "Failed to initialize Metal\n");
        exit(1);
    }

    // Load Metal library and create pipelines
    if (!loadMetalLibrary()) {
        fprintf(stderr, "Failed to load Metal library\n");
        exit(1);
    }

    if (!createPipelines()) {
        fprintf(stderr, "Failed to create Metal pipelines\n");
        exit(1);
    }

    // Allocate buffers
    if (!allocateBuffers(gTableXCPU, gTableYCPU)) {
        fprintf(stderr, "Failed to allocate Metal buffers\n");
        exit(1);
    }

    // Upload vanity pattern to GPU
    id<MTLBuffer> vanityBuf = (__bridge id<MTLBuffer>)vanityPatternBuffer;
    memcpy([vanityBuf contents], vanityPattern, vanityLen);

    // Upload start offset
    id<MTLBuffer> offsetBuf = (__bridge id<MTLBuffer>)startOffsetBuffer;
    memcpy([offsetBuf contents], startOffsetInput, 32);

    // Calculate search space for sequential mode
    if (searchMode == SEARCH_SEQUENTIAL) {
        // Simplified calculation - will refine later
        searchSpaceSize = METAL_TOTAL_THREADS * METAL_KEYS_PER_THREAD;
        totalIterations = (1ULL << 48) / searchSpaceSize; // Search subset of keyspace
    }

    printf("MetalGPUMiner initialized successfully\n");
    printf("  Device: %s\n", [(__bridge id<MTLDevice>)device name].UTF8String);
    printf("  Threadgroups: %d\n", METAL_THREADGROUPS_PER_GRID);
    printf("  Threads per threadgroup: %d\n", METAL_THREADGROUP_SIZE);
    printf("  Total threads: %d\n", METAL_TOTAL_THREADS);
    printf("  Keys per iteration: %llu\n", (unsigned long long)(METAL_TOTAL_THREADS * METAL_KEYS_PER_THREAD));
    printf("\n");
}

MetalGPUMiner::~MetalGPUMiner() {
    doFreeMemory();
}

bool MetalGPUMiner::initializeMetal() {
    @autoreleasepool {
        // Create Metal device
        id<MTLDevice> mtlDevice = MTLCreateSystemDefaultDevice();
        if (!mtlDevice) {
            fprintf(stderr, "Metal is not supported on this device\n");
            return false;
        }
        device = (__bridge_retained MTLDevice *)mtlDevice;

        // Create command queue
        id<MTLCommandQueue> mtlQueue = [mtlDevice newCommandQueue];
        if (!mtlQueue) {
            fprintf(stderr, "Failed to create Metal command queue\n");
            return false;
        }
        commandQueue = (__bridge_retained MTLCommandQueue *)mtlQueue;

        return true;
    }
}

bool MetalGPUMiner::loadMetalLibrary() {
    @autoreleasepool {
        id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device;
        NSError *error = nil;

        // Try to load pre-compiled library from file
        NSURL *libraryURL = [[NSBundle mainBundle] URLForResource:@"default" withExtension:@"metallib"];
        if (!libraryURL) {
            // Try current directory
            libraryURL = [NSURL fileURLWithPath:@"default.metallib"];
        }

        id<MTLLibrary> mtlLibrary = nil;
        if (libraryURL && [[NSFileManager defaultManager] fileExistsAtPath:[libraryURL path]]) {
            mtlLibrary = [mtlDevice newLibraryWithURL:libraryURL error:&error];
            if (mtlLibrary) {
                printf("Loaded Metal library from: %s\n", [[libraryURL path] UTF8String]);
            }
        }

        // If file-based loading failed, try to compile from source (for development)
        if (!mtlLibrary) {
            printf("Pre-compiled library not found, attempting to compile from source...\n");

            // Try to load source file
            NSString *sourcePath = @"src/GPU/metal/MetalKernels.metal";
            NSError *readError = nil;
            NSString *source = [NSString stringWithContentsOfFile:sourcePath
                                                         encoding:NSUTF8StringEncoding
                                                            error:&readError];

            if (source) {
                MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
                options.fastMathEnabled = YES;
                mtlLibrary = [mtlDevice newLibraryWithSource:source options:options error:&error];
                if (mtlLibrary) {
                    printf("Compiled Metal library from source: %s\n", [sourcePath UTF8String]);
                }
            }
        }

        if (!mtlLibrary) {
            fprintf(stderr, "Failed to load Metal library: %s\n",
                    error ? [[error localizedDescription] UTF8String] : "unknown error");
            return false;
        }

        library = (__bridge_retained MTLLibrary *)mtlLibrary;
        return true;
    }
}

bool MetalGPUMiner::createPipelines() {
    @autoreleasepool {
        id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device;
        id<MTLLibrary> mtlLibrary = (__bridge id<MTLLibrary>)library;
        NSError *error = nil;

        // Create random mode pipeline
        id<MTLFunction> randomFunction = [mtlLibrary newFunctionWithName:@"nostrVanityKernel_random"];
        if (randomFunction) {
            id<MTLComputePipelineState> pipeline = [mtlDevice newComputePipelineStateWithFunction:randomFunction error:&error];
            if (pipeline) {
                randomPipeline = (__bridge_retained MTLComputePipelineState *)pipeline;
                printf("Created random mode pipeline\n");
            } else {
                fprintf(stderr, "Failed to create random pipeline: %s\n", [[error localizedDescription] UTF8String]);
            }
        } else {
            printf("Warning: Random mode kernel not found in library (will be implemented in Phase 4)\n");
        }

        // Create sequential mode pipeline
        id<MTLFunction> seqFunction = [mtlLibrary newFunctionWithName:@"nostrVanityKernel_sequential"];
        if (seqFunction) {
            id<MTLComputePipelineState> pipeline = [mtlDevice newComputePipelineStateWithFunction:seqFunction error:&error];
            if (pipeline) {
                sequentialPipeline = (__bridge_retained MTLComputePipelineState *)pipeline;
                printf("Created sequential mode pipeline\n");
            } else {
                fprintf(stderr, "Failed to create sequential pipeline: %s\n", [[error localizedDescription] UTF8String]);
            }
        } else {
            printf("Warning: Sequential mode kernel not found in library (will be implemented in Phase 4)\n");
        }

        // At least one pipeline should succeed for now (or we're still in Phase 2-3)
        return true;
    }
}

bool MetalGPUMiner::allocateBuffers(const uint8_t *gTableXCPU, const uint8_t *gTableYCPU) {
    @autoreleasepool {
        id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device;

        // Calculate buffer sizes
        size_t gTableSize = COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT;
        size_t resultsSize = METAL_TOTAL_THREADS * sizeof(uint8_t);
        size_t privKeysSize = METAL_TOTAL_THREADS * 32;
        size_t pubKeysSize = METAL_TOTAL_THREADS * 32;

        printf("Allocating Metal buffers...\n");
        printf("  GTable X: %.2f MB\n", gTableSize / (1024.0 * 1024.0));
        printf("  GTable Y: %.2f MB\n", gTableSize / (1024.0 * 1024.0));
        printf("  Results: %.2f KB\n", resultsSize / 1024.0);
        printf("  Private keys: %.2f KB\n", privKeysSize / 1024.0);
        printf("  Public keys: %.2f KB\n", pubKeysSize / 1024.0);

        // Allocate GTable buffers (read-only, shared with CPU)
        id<MTLBuffer> gTableX = [mtlDevice newBufferWithBytes:gTableXCPU
                                                       length:gTableSize
                                                      options:MTLResourceStorageModeShared];
        if (!gTableX) {
            fprintf(stderr, "Failed to allocate GTable X buffer\n");
            return false;
        }
        gTableXBuffer = (__bridge_retained MTLBuffer *)gTableX;

        id<MTLBuffer> gTableY = [mtlDevice newBufferWithBytes:gTableYCPU
                                                       length:gTableSize
                                                      options:MTLResourceStorageModeShared];
        if (!gTableY) {
            fprintf(stderr, "Failed to allocate GTable Y buffer\n");
            return false;
        }
        gTableYBuffer = (__bridge_retained MTLBuffer *)gTableY;

        // Allocate vanity pattern buffer
        id<MTLBuffer> vanityBuf = [mtlDevice newBufferWithLength:MAX_VANITY_HEX_LEN
                                                          options:MTLResourceStorageModeShared];
        if (!vanityBuf) {
            fprintf(stderr, "Failed to allocate vanity pattern buffer\n");
            return false;
        }
        vanityPatternBuffer = (__bridge_retained MTLBuffer *)vanityBuf;

        // Allocate start offset buffer
        id<MTLBuffer> offsetBuf = [mtlDevice newBufferWithLength:32
                                                          options:MTLResourceStorageModeShared];
        if (!offsetBuf) {
            fprintf(stderr, "Failed to allocate start offset buffer\n");
            return false;
        }
        startOffsetBuffer = (__bridge_retained MTLBuffer *)offsetBuf;

        // Allocate results buffer (GPU writes, CPU reads)
        id<MTLBuffer> resBuf = [mtlDevice newBufferWithLength:resultsSize
                                                       options:MTLResourceStorageModeShared];
        if (!resBuf) {
            fprintf(stderr, "Failed to allocate results buffer\n");
            return false;
        }
        resultsBuffer = (__bridge_retained MTLBuffer *)resBuf;

        // Allocate private keys buffer
        id<MTLBuffer> privBuf = [mtlDevice newBufferWithLength:privKeysSize
                                                        options:MTLResourceStorageModeShared];
        if (!privBuf) {
            fprintf(stderr, "Failed to allocate private keys buffer\n");
            return false;
        }
        privKeysBuffer = (__bridge_retained MTLBuffer *)privBuf;

        // Allocate public keys buffer
        id<MTLBuffer> pubBuf = [mtlDevice newBufferWithLength:pubKeysSize
                                                       options:MTLResourceStorageModeShared];
        if (!pubBuf) {
            fprintf(stderr, "Failed to allocate public keys buffer\n");
            return false;
        }
        pubKeysBuffer = (__bridge_retained MTLBuffer *)pubBuf;

        // Set up CPU-side pointers to shared buffers
        outputFoundCPU = (uint8_t *)[resBuf contents];
        outputPrivKeysCPU = (uint8_t *)[privBuf contents];
        outputPubKeysCPU = (uint8_t *)[pubBuf contents];

        // Clear results buffer
        memset(outputFoundCPU, 0, resultsSize);

        printf("Metal buffers allocated successfully\n");
        return true;
    }
}

void MetalGPUMiner::doIteration(uint64_t iteration) {
    @autoreleasepool {
        // Clear results buffers (both CPU and GPU)
        memset(outputFoundCPU, 0, METAL_TOTAL_THREADS);
        memset([(id<MTLBuffer>)resultsBuffer contents], 0, METAL_TOTAL_THREADS);
        memset([(id<MTLBuffer>)privKeysBuffer contents], 0, METAL_TOTAL_THREADS * 32);
        memset([(id<MTLBuffer>)pubKeysBuffer contents], 0, METAL_TOTAL_THREADS * 32);

        id<MTLCommandBuffer> commandBuffer = [(MTLCommandQueue *)commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];

        // Select appropriate pipeline
        id<MTLComputePipelineState> pipeline = (searchMode == SEARCH_RANDOM) ?
            (id<MTLComputePipelineState>)randomPipeline :
            (id<MTLComputePipelineState>)sequentialPipeline;

        [encoder setComputePipelineState:pipeline];

        // Set buffers (common to both modes)
        [encoder setBuffer:(id<MTLBuffer>)gTableXBuffer offset:0 atIndex:0];
        [encoder setBuffer:(id<MTLBuffer>)gTableYBuffer offset:0 atIndex:1];
        [encoder setBuffer:(id<MTLBuffer>)vanityPatternBuffer offset:0 atIndex:2];

        if (searchMode == SEARCH_SEQUENTIAL) {
            // Sequential mode - additional buffers
            [encoder setBuffer:(id<MTLBuffer>)startOffsetBuffer offset:0 atIndex:3];
            [encoder setBuffer:(id<MTLBuffer>)resultsBuffer offset:0 atIndex:4];
            [encoder setBuffer:(id<MTLBuffer>)privKeysBuffer offset:0 atIndex:5];
            [encoder setBuffer:(id<MTLBuffer>)pubKeysBuffer offset:0 atIndex:6];

            // Set constants (iteration, vanityLen, vanityMode)
            [encoder setBytes:&iteration length:sizeof(uint64_t) atIndex:7];
            [encoder setBytes:&vanityLen length:sizeof(uint32_t) atIndex:8];
            uint32_t mode = (uint32_t)vanityMode;
            [encoder setBytes:&mode length:sizeof(uint32_t) atIndex:9];
        } else {
            // Random mode
            [encoder setBuffer:(id<MTLBuffer>)resultsBuffer offset:0 atIndex:3];
            [encoder setBuffer:(id<MTLBuffer>)privKeysBuffer offset:0 atIndex:4];
            [encoder setBuffer:(id<MTLBuffer>)pubKeysBuffer offset:0 atIndex:5];

            // Set constants (vanityLen, vanityMode)
            [encoder setBytes:&vanityLen length:sizeof(uint32_t) atIndex:6];
            uint32_t mode = (uint32_t)vanityMode;
            [encoder setBytes:&mode length:sizeof(uint32_t) atIndex:7];
        }

        // Dispatch threads
        MTLSize gridSize = MTLSizeMake(METAL_TOTAL_THREADS, 1, 1);
        MTLSize threadgroupSize = MTLSizeMake(METAL_THREADS_PER_THREADGROUP, 1, 1);
        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        // Check for errors
        if ([commandBuffer error]) {
            NSError *error = [commandBuffer error];
            printf("\n[ERROR] Metal command buffer failed: %s\n", [[error localizedDescription] UTF8String]);
        }

        // Copy results back to CPU
        memcpy(outputFoundCPU, [(id<MTLBuffer>)resultsBuffer contents], METAL_TOTAL_THREADS);
        memcpy(outputPrivKeysCPU, [(id<MTLBuffer>)privKeysBuffer contents], METAL_TOTAL_THREADS * 32);
        memcpy(outputPubKeysCPU, [(id<MTLBuffer>)pubKeysBuffer contents], METAL_TOTAL_THREADS * 32);

        // Debug: Print first public key on first iteration
        if (currentIteration == 0) {
            printf("\n[DEBUG] Iteration %llu, First public key (thread 0): ", (unsigned long long)iteration);
            for (int i = 0; i < 8; i++) {  // Just first 8 bytes
                printf("%02x", outputPubKeysCPU[i]);
            }
            printf("...\n");
            printf("[DEBUG] Results[0]: %d, PrivKey[0]: %02x%02x%02x%02x\n",
                   outputFoundCPU[0], outputPrivKeysCPU[0], outputPrivKeysCPU[1],
                   outputPrivKeysCPU[2], outputPrivKeysCPU[3]);
        }

        // Update stats
        keysGenerated += METAL_TOTAL_THREADS * METAL_KEYS_PER_THREAD;
        currentIteration = iteration;
    }
}

bool MetalGPUMiner::checkAndPrintResults() {
    bool foundAny = false;

    for (int idxThread = 0; idxThread < METAL_TOTAL_THREADS; idxThread++) {
        if (outputFoundCPU[idxThread] > 0) {
            // Get private and public keys
            uint8_t *privKey = &outputPrivKeysCPU[idxThread * 32];
            uint8_t *pubKey = &outputPubKeysCPU[idxThread * 32];

            // If we converted from bech32 to hex, verify the full bech32 pattern
            if (needsBech32Verification) {
                char npub[64];
                encode_npub_cpu(pubKey, npub);

                // Check if full bech32 pattern matches
                bool bech32Match = false;
                size_t patternLen = strlen(originalBech32Pattern);

                if (originalBech32Mode == VANITY_BECH32_PREFIX) {
                    bech32Match = (strncmp(npub, originalBech32Pattern, patternLen) == 0);
                } else if (originalBech32Mode == VANITY_BECH32_SUFFIX) {
                    size_t npubLen = strlen(npub) - 6;
                    if (npubLen >= patternLen) {
                        bech32Match = (strncmp(npub + npubLen - patternLen, originalBech32Pattern, patternLen) == 0);
                    }
                } else if (originalBech32Mode == VANITY_BECH32_BOTH) {
                    size_t halfLen = patternLen / 2;
                    bool prefixMatch = (strncmp(npub, originalBech32Pattern, halfLen) == 0);
                    size_t npubLen = strlen(npub) - 6;
                    size_t suffixLen = patternLen - halfLen;
                    bool suffixMatch = (npubLen >= suffixLen) &&
                                       (strncmp(npub + npubLen - suffixLen, originalBech32Pattern + halfLen, suffixLen) == 0);
                    bech32Match = prefixMatch && suffixMatch;
                }

                if (!bech32Match) {
                    continue;  // Skip false positive
                }
            }

            foundAny = true;
            matchesFound++;

            printf("\n========== MATCH FOUND ==========\n");
            printf("Private Key (hex): ");
            for (int i = 0; i < 32; i++) {
                printf("%02x", privKey[i]);
            }
            printf("\n");

            printf("Public Key (hex):  ");
            for (int i = 0; i < 32; i++) {
                printf("%02x", pubKey[i]);
            }
            printf("\n");

            // If we verified bech32 or in bech32 mode, also display the npub
            if (needsBech32Verification || vanityMode >= VANITY_BECH32_PREFIX) {
                char npub[64];
                encode_npub_cpu(pubKey, npub);
                printf("Public Key (npub): npub1%s\n", npub);
            }

            printf("Total keys searched: %llu\n", (unsigned long long)keysGenerated);
            printf("=================================\n\n");

            // Write to file
            FILE *file = fopen("keys.txt", "a");
            if (file != NULL) {
                fprintf(file, "\n========== MATCH FOUND ==========\n");
                fprintf(file, "Private Key (hex): ");
                for (int i = 0; i < 32; i++) {
                    fprintf(file, "%02x", privKey[i]);
                }
                fprintf(file, "\n");

                fprintf(file, "Public Key (hex):  ");
                for (int i = 0; i < 32; i++) {
                    fprintf(file, "%02x", pubKey[i]);
                }
                fprintf(file, "\n");

                if (needsBech32Verification || vanityMode >= VANITY_BECH32_PREFIX) {
                    char npub[64];
                    encode_npub_cpu(pubKey, npub);
                    fprintf(file, "Public Key (npub): npub1%s\n", npub);
                }

                fprintf(file, "Total keys searched: %llu\n", (unsigned long long)keysGenerated);
                fprintf(file, "=================================\n\n");
                fclose(file);
            }
        }
    }

    return foundAny;
}

void MetalGPUMiner::doFreeMemory() {
    printf("\nMetalGPUMiner freeing memory... ");

    // Release Metal objects
    if (gTableXBuffer) CFRelease(gTableXBuffer);
    if (gTableYBuffer) CFRelease(gTableYBuffer);
    if (vanityPatternBuffer) CFRelease(vanityPatternBuffer);
    if (startOffsetBuffer) CFRelease(startOffsetBuffer);
    if (resultsBuffer) CFRelease(resultsBuffer);
    if (privKeysBuffer) CFRelease(privKeysBuffer);
    if (pubKeysBuffer) CFRelease(pubKeysBuffer);

    if (randomPipeline) CFRelease(randomPipeline);
    if (sequentialPipeline) CFRelease(sequentialPipeline);
    if (library) CFRelease(library);
    if (commandQueue) CFRelease(commandQueue);
    if (device) CFRelease(device);

    printf("Done\n");
}

uint64_t MetalGPUMiner::getKeysGenerated() const {
    return keysGenerated;
}

uint64_t MetalGPUMiner::getMatchesFound() const {
    return matchesFound;
}

bool MetalGPUMiner::saveCheckpoint(const char *filename) {
    // Placeholder for Phase 5
    if (searchMode != SEARCH_SEQUENTIAL) {
        return false;
    }

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        return false;
    }

    fprintf(fp, "# Rummage Metal Sequential Search Checkpoint\n");
    fprintf(fp, "iteration=%llu\n", (unsigned long long)currentIteration);
    fprintf(fp, "keysGenerated=%llu\n", (unsigned long long)keysGenerated);
    fprintf(fp, "startOffset=");
    for (int i = 0; i < 32; i++) {
        fprintf(fp, "%02x", startOffset[i]);
    }
    fprintf(fp, "\n");

    fclose(fp);
    return true;
}

bool MetalGPUMiner::loadCheckpoint(const char *filename) {
    // Placeholder for Phase 5
    if (searchMode != SEARCH_SEQUENTIAL) {
        return false;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;

        if (strncmp(line, "iteration=", 10) == 0) {
            currentIteration = strtoull(line + 10, NULL, 10);
        } else if (strncmp(line, "keysGenerated=", 14) == 0) {
            keysGenerated = strtoull(line + 14, NULL, 10);
        }
    }

    fclose(fp);
    return true;
}

double MetalGPUMiner::getSearchProgress() const {
    if (searchMode != SEARCH_SEQUENTIAL || totalIterations == 0) {
        return 0.0;
    }
    return (double)currentIteration / (double)totalIterations;
}

uint64_t MetalGPUMiner::getCurrentIteration() const {
    return currentIteration;
}

uint64_t MetalGPUMiner::getTotalIterations() const {
    return totalIterations;
}

void MetalGPUMiner::setBech32Verification(const char *originalPattern, VanityMode originalMode) {
    this->needsBech32Verification = true;
    strncpy(this->originalBech32Pattern, originalPattern, MAX_VANITY_BECH32_LEN);
    this->originalBech32Pattern[MAX_VANITY_BECH32_LEN] = '\0';
    this->originalBech32Mode = originalMode;
}
