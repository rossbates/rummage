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
    // Placeholder for Phase 5 - kernel dispatch
    // Will implement actual GPU kernel execution
    currentIteration = iteration;

    // For now, just increment keys generated count
    keysGenerated += METAL_TOTAL_THREADS * METAL_KEYS_PER_THREAD;
}

bool MetalGPUMiner::checkAndPrintResults() {
    // Placeholder for Phase 5 - results checking
    // Will implement actual result verification and printing
    return false;
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
