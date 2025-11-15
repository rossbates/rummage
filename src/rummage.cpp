/*
 * Rummage - GPU Nostr Vanity Key Miner
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

#include <cstring>
#include <cmath>
#include <iostream>
#include <cassert>
#include <chrono>
#include <signal.h>
#include "GPU/GPUMinerFactory.h"
#include "GPU/NostrUtils.h"
#include "CPU/SECP256k1.h"

// Global flag for graceful shutdown
volatile sig_atomic_t keepRunning = 1;

void signalHandler(int sig) {
    printf("\nReceived signal %d, stopping mining...\n", sig);
    keepRunning = 0;
}

void loadGTable(uint8_t *gTableX, uint8_t *gTableY) {
    std::cout << "Generating GTable (this may take a minute)..." << std::endl;

    Secp256K1 *secp = new Secp256K1();
    secp->Init();

    for (int i = 0; i < NUM_GTABLE_CHUNK; i++)
    {
        for (int j = 0; j < NUM_GTABLE_VALUE - 1; j++)
        {
            int element = (i * NUM_GTABLE_VALUE) + j;
            Point p = secp->GTable[element];
            for (int b = 0; b < 32; b++) {
                gTableX[(element * SIZE_GTABLE_POINT) + b] = p.x.GetByte64(b);
                gTableY[(element * SIZE_GTABLE_POINT) + b] = p.y.GetByte64(b);
            }
        }
    }

    delete secp;
    std::cout << "GTable generation complete!" << std::endl;
}

void printUsage(const char *progName) {
    printf("Usage: %s [OPTIONS]\n", progName);
    printf("\nOptions (Hex Mode - searches raw public key hex):\n");
    printf("  --prefix <hex>          Search for hex prefix in public key\n");
    printf("  --suffix <hex>          Search for hex suffix in public key\n");
    printf("  --both <hex>            Search for hex pattern as both prefix and suffix\n");
    printf("\nOptions (Bech32 Mode - searches npub address):\n");
    printf("  --npub-prefix <str>     Search for prefix in npub address\n");
    printf("  --npub-suffix <str>     Search for suffix in npub address\n");
    printf("  --npub-both <str>       Search for pattern in both prefix and suffix of npub\n");
    printf("\nSearch Mode Options:\n");
    printf("  --sequential            Use sequential exhaustive search\n");
    printf("  --checkpoint <file>     Checkpoint file for sequential mode (default: checkpoint.txt)\n");
    printf("\n  --help                  Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s --prefix cafe\n", progName);
    printf("  %s --npub-prefix alice\n", progName);
    printf("  %s --npub-prefix satoshi --sequential\n", progName);
}

// Validate hex string
bool isValidHex(const char *str) {
    for (int i = 0; i < strlen(str); i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

// Validate bech32 string (charset: qpzry9x8gf2tvdw0s3jn54khce6mua7l)
bool isValidBech32(const char *str) {
    const char *bech32_charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    for (int i = 0; i < strlen(str); i++) {
        char c = tolower(str[i]);
        bool found = false;
        for (int j = 0; j < 32; j++) {
            if (c == bech32_charset[j]) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// Convert bech32 pattern to hex pattern (for fast GPU mining)
std::string bech32ToHexPattern(const char *bech32Pattern) {
    const char *bech32_charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

    // Convert each bech32 char to 5-bit value
    std::string bits;
    for (int i = 0; i < strlen(bech32Pattern); i++) {
        char c = tolower(bech32Pattern[i]);
        // Find position in charset
        int value = 0;
        for (int j = 0; j < 32; j++) {
            if (c == bech32_charset[j]) {
                value = j;
                break;
            }
        }
        // Convert to 5-bit binary
        for (int b = 4; b >= 0; b--) {
            bits += ((value >> b) & 1) ? '1' : '0';
        }
    }

    // Convert bits to hex (take full bytes only)
    std::string hexPattern;
    for (size_t i = 0; i + 8 <= bits.length(); i += 8) {
        int byte = 0;
        for (int b = 0; b < 8; b++) {
            byte = (byte << 1) | (bits[i + b] - '0');
        }
        char hexByte[3];
        snprintf(hexByte, sizeof(hexByte), "%02x", byte);
        hexPattern += hexByte;
    }

    return hexPattern;
}

int main(int argc, char **argv) {
    // Disable stdout buffering for real-time output when redirected to file
    setbuf(stdout, NULL);

    printf("\n");
    printf("        ╔═══════╗\n");
    printf("        ║       ║\n");
    printf("        ║ ╰───╯ ║   R U M M A G E\n");
    printf("        ║       ║   npub mining tool\n");
    printf("        ╚═══════╝\n");
    printf("\n");

    // Parse command line arguments
    const char *vanityPattern = NULL;
    VanityMode vanityMode = VANITY_HEX_PREFIX;
    bool isBech32Mode = false;
    SearchMode searchMode = SEARCH_RANDOM;  // Default to random search
    const char *checkpointFile = "checkpoint.txt";

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--prefix") == 0) {
            if (i + 1 < argc) {
                vanityPattern = argv[++i];
                vanityMode = VANITY_HEX_PREFIX;
                isBech32Mode = false;
            } else {
                printf("Error: --prefix requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--suffix") == 0) {
            if (i + 1 < argc) {
                vanityPattern = argv[++i];
                vanityMode = VANITY_HEX_SUFFIX;
                isBech32Mode = false;
            } else {
                printf("Error: --suffix requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--both") == 0) {
            if (i + 1 < argc) {
                vanityPattern = argv[++i];
                vanityMode = VANITY_HEX_BOTH;
                isBech32Mode = false;
            } else {
                printf("Error: --both requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--npub-prefix") == 0) {
            if (i + 1 < argc) {
                vanityPattern = argv[++i];
                vanityMode = VANITY_BECH32_PREFIX;
                isBech32Mode = true;
            } else {
                printf("Error: --npub-prefix requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--npub-suffix") == 0) {
            if (i + 1 < argc) {
                vanityPattern = argv[++i];
                vanityMode = VANITY_BECH32_SUFFIX;
                isBech32Mode = true;
            } else {
                printf("Error: --npub-suffix requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--npub-both") == 0) {
            if (i + 1 < argc) {
                vanityPattern = argv[++i];
                vanityMode = VANITY_BECH32_BOTH;
                isBech32Mode = true;
            } else {
                printf("Error: --npub-both requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--sequential") == 0) {
            searchMode = SEARCH_SEQUENTIAL;
        } else if (strcmp(argv[i], "--checkpoint") == 0) {
            if (i + 1 < argc) {
                checkpointFile = argv[++i];
            } else {
                printf("Error: --checkpoint requires a filename\n");
                return 1;
            }
        }
    }

    if (vanityPattern == NULL) {
        printf("Error: You must specify a vanity pattern\n");
        printUsage(argv[0]);
        return 1;
    }

    // Store original pattern for display
    const char *originalPattern = vanityPattern;
    std::string hexPatternStr;

    // Store original bech32 pattern before conversion (for verification)
    const char *originalBech32Pattern = NULL;
    VanityMode originalBech32Mode = vanityMode;

    // Validate pattern and convert bech32 to hex if needed
    if (isBech32Mode) {
        if (!isValidBech32(vanityPattern)) {
            printf("Error: Pattern must use valid bech32 characters (qpzry9x8gf2tvdw0s3jn54khce6mua7l)\n");
            printf("Note: Characters '1', 'b', 'i', 'o' are NOT valid in bech32\n");
            return 1;
        }

        // Save original pattern for later verification
        originalBech32Pattern = strdup(vanityPattern);

        // Convert bech32 to hex for FAST GPU pre-filtering!
        hexPatternStr = bech32ToHexPattern(vanityPattern);
        vanityPattern = hexPatternStr.c_str();

        // Switch to hex mode for fast GPU search
        if (vanityMode == VANITY_BECH32_PREFIX) {
            vanityMode = VANITY_HEX_PREFIX;
        } else if (vanityMode == VANITY_BECH32_SUFFIX) {
            vanityMode = VANITY_HEX_SUFFIX;
        } else if (vanityMode == VANITY_BECH32_BOTH) {
            vanityMode = VANITY_HEX_BOTH;
        }

        printf("Converted npub pattern '%s' -> hex pattern '%s' (for fast pre-filtering)\n", originalBech32Pattern, vanityPattern);
        printf("Will verify full bech32 match after hex pre-filter\n");
        isBech32Mode = false; // Now using hex mode for GPU
    } else {
        if (!isValidHex(vanityPattern)) {
            printf("Error: Pattern must be valid hexadecimal (0-9, a-f)\n");
            return 1;
        }
    }

    // Check pattern length
    size_t patternLen = strlen(vanityPattern);
    if (isBech32Mode) {
        if (patternLen == 0 || patternLen > MAX_VANITY_BECH32_LEN) {
            printf("Error: Bech32 pattern length must be between 1 and %d characters\n", MAX_VANITY_BECH32_LEN);
            return 1;
        }
    } else {
        if (patternLen == 0 || patternLen > MAX_VANITY_HEX_LEN) {
            printf("Error: Hex pattern length must be between 1 and %d characters\n", MAX_VANITY_HEX_LEN);
            return 1;
        }
    }

    // Convert pattern to lowercase
    char *lowerPattern = (char *)malloc(patternLen + 1);
    for (size_t i = 0; i < patternLen; i++) {
        lowerPattern[i] = tolower(vanityPattern[i]);
    }
    lowerPattern[patternLen] = '\0';

    printf("\nConfiguration:\n");
    printf("  Mode:    ");
    if (vanityMode == VANITY_HEX_PREFIX) {
        printf("Hex Prefix\n");
        printf("  Pattern: %s***...\n", lowerPattern);
    } else if (vanityMode == VANITY_HEX_SUFFIX) {
        printf("Hex Suffix\n");
        printf("  Pattern: ...***%s\n", lowerPattern);
    } else if (vanityMode == VANITY_HEX_BOTH) {
        printf("Hex Both (prefix + suffix)\n");
        size_t half = patternLen / 2;
        char prefix[half + 1];
        char suffix[patternLen - half + 1];
        strncpy(prefix, lowerPattern, half);
        prefix[half] = '\0';
        strcpy(suffix, lowerPattern + half);
        printf("  Pattern: %s***...***%s\n", prefix, suffix);
    } else if (vanityMode == VANITY_BECH32_PREFIX) {
        printf("Bech32 Prefix (npub address)\n");
        printf("  Pattern: npub1%s***...\n", lowerPattern);
        printf("  Length:  %zu characters\n", patternLen);
    } else if (vanityMode == VANITY_BECH32_SUFFIX) {
        printf("Bech32 Suffix (npub address)\n");
        printf("  Pattern: npub1...***%s\n", lowerPattern);
        printf("  Length:  %zu characters\n", patternLen);
    } else if (vanityMode == VANITY_BECH32_BOTH) {
        printf("Bech32 Both (prefix + suffix)\n");
        size_t half = patternLen / 2;
        char prefix[half + 1];
        char suffix[patternLen - half + 1];
        strncpy(prefix, lowerPattern, half);
        prefix[half] = '\0';
        strcpy(suffix, lowerPattern + half);
        printf("  Pattern: npub1%s***...***%s\n", prefix, suffix);
        printf("  Length:  %zu characters total\n", patternLen);
    }
    printf("\n");

    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Allocate and load GTable
    uint8_t* gTableXCPU = new uint8_t[COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT];
    uint8_t* gTableYCPU = new uint8_t[COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT];

    loadGTable(gTableXCPU, gTableYCPU);

    // Generate starting offset or load from checkpoint
    uint8_t startOffset[32];
    bool resumingFromCheckpoint = false;

    if (searchMode == SEARCH_SEQUENTIAL) {
        // Check if checkpoint file exists
        FILE *checkFile = fopen(checkpointFile, "r");
        if (checkFile) {
            // Checkpoint exists - try to load offset from it
            char line[512];
            bool offsetLoaded = false;

            while (fgets(line, sizeof(line), checkFile)) {
                if (line[0] == '#') continue;

                if (strncmp(line, "startOffset=", 12) == 0) {
                    char *hexStr = line + 12;
                    for (int i = 0; i < 32; i++) {
                        char byteStr[3] = {hexStr[i*2], hexStr[i*2+1], '\0'};
                        startOffset[i] = (uint8_t)strtol(byteStr, NULL, 16);
                    }
                    offsetLoaded = true;
                    resumingFromCheckpoint = true;
                    break;
                }
            }
            fclose(checkFile);

            if (!offsetLoaded) {
                fprintf(stderr, "Error: Checkpoint file exists but missing startOffset\n");
                return 1;
            }

            printf("Loaded starting offset from checkpoint file\n");
        } else {
            // No checkpoint - generate new random offset
            FILE *urandom = fopen("/dev/urandom", "rb");
            if (!urandom) {
                fprintf(stderr, "Error: Cannot open /dev/urandom for secure random generation\n");
                return 1;
            }

            size_t bytesRead = fread(startOffset, 1, 32, urandom);
            fclose(urandom);

            if (bytesRead != 32) {
                fprintf(stderr, "Error: Failed to read 32 bytes from /dev/urandom\n");
                return 1;
            }

            printf("Generated random 256-bit starting offset for sequential search\n");
        }

        printf("Offset (hex): ");
        for (int i = 0; i < 32; i++) {
            printf("%02x", startOffset[i]);
        }
        printf("\n");
        printf("WARNING: This offset will be saved in the checkpoint file - protect it!\n\n");
    } else {
        // Random mode: Use timestamp to seed RNG (doesn't matter for security)
        uint64_t timestamp = (uint64_t)time(NULL);
        memset(startOffset, 0, 32);
        memcpy(startOffset + 24, &timestamp, 8);  // Put timestamp in last 8 bytes
    }

    // Calculate original bech32 pattern length for search space calculation
    int bech32PatternLen = 0;
    if (originalBech32Pattern != NULL) {
        bech32PatternLen = strlen(originalBech32Pattern);
    }

    // Initialize GPU miner using factory pattern
    IGPUMiner *miner = createGPUMiner(
        gTableXCPU,
        gTableYCPU,
        lowerPattern,
        vanityMode,
        startOffset,
        searchMode,
        bech32PatternLen  // Pass original bech32 length for correct search space
    );

    // If we converted from bech32 to hex, enable verification
    if (originalBech32Pattern != NULL) {
        miner->setBech32Verification(originalBech32Pattern, originalBech32Mode);
    }

    // Load checkpoint if in sequential mode
    if (searchMode == SEARCH_SEQUENTIAL) {
        if (resumingFromCheckpoint && miner->loadCheckpoint(checkpointFile)) {
            printf("Resumed from checkpoint\n");
        } else if (!resumingFromCheckpoint) {
            printf("Starting new sequential search (no checkpoint found)\n");
        }
    }

    printf("\nMining started! Press Ctrl+C to stop.\n");
    printf("Keys will be saved to: keys.txt\n");
    if (searchMode == SEARCH_SEQUENTIAL) {
        printf("Checkpoints will be saved to: %s\n", checkpointFile);
    }
    printf("\n");

    uint64_t iteration = 0;
    auto startTime = std::chrono::system_clock::now();
    auto lastReportTime = startTime;
    auto lastCheckpointTime = startTime;

    while (keepRunning) {
        // Run one iteration
        miner->doIteration(iteration);

        // Check for results
        bool found = miner->checkAndPrintResults();

        // Print statistics every 10 iterations
        if (iteration % 10 == 0) {
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime).count();

            if (elapsed > 5000) { // Report every 5 seconds
                uint64_t keysGenerated = miner->getKeysGenerated();
                auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

                if (totalElapsed > 0) {
                    double rate = keysGenerated / (double)totalElapsed;
                    if (searchMode == SEARCH_SEQUENTIAL) {
                        double progress = miner->getSearchProgress() * 100.0;
                        printf("Progress: %.2f%% | %lu keys searched, %.2f keys/sec\n", progress, keysGenerated, rate);
                    } else {
                        printf("Stats: %lu keys searched, %.2f keys/sec\n", keysGenerated, rate);
                    }
                    fflush(stdout);  // Force output to file immediately
                }

                lastReportTime = now;
            }

            // Save checkpoint every 60 seconds in sequential mode
            if (searchMode == SEARCH_SEQUENTIAL) {
                auto checkpointElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastCheckpointTime).count();
                if (checkpointElapsed >= 60) {
                    miner->saveCheckpoint(checkpointFile);
                    lastCheckpointTime = now;
                }
            }
        }

        // Check if sequential search is complete
        if (searchMode == SEARCH_SEQUENTIAL && miner->getSearchProgress() >= 1.0) {
            printf("\nSequential search complete! Exhausted entire search space.\n");
            if (miner->getMatchesFound() == 0) {
                printf("No matches found in entire search space.\n");
            }
            break;
        }

        // If we found a match and want to stop, break
        // (For continuous mining, remove this break)
        if (found) {
            printf("\nMatch found! Continuing to search for more matches...\n");
            printf("(Press Ctrl+C to stop mining)\n\n");
            fflush(stdout);  // Force output to file immediately
            // break; // Uncomment to stop after first match
        }

        iteration++;
    }

    printf("\nShutting down...\n");

    // Save final checkpoint in sequential mode
    if (searchMode == SEARCH_SEQUENTIAL) {
        miner->saveCheckpoint(checkpointFile);
        printf("Final checkpoint saved\n");
    }

    // Print final statistics
    uint64_t keysGenerated = miner->getKeysGenerated();
    uint64_t matchesFound = miner->getMatchesFound();
    auto endTime = std::chrono::system_clock::now();
    auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

    printf("\nFinal Statistics:\n");
    if (searchMode == SEARCH_SEQUENTIAL) {
        printf("  Search progress: %.2f%%\n", miner->getSearchProgress() * 100.0);
    }
    printf("  Total keys searched: %lu\n", keysGenerated);
    printf("  Matches found: %lu\n", matchesFound);
    if (totalElapsed > 0) {
        double rate = keysGenerated / (double)totalElapsed;
        printf("  Average rate: %.2f keys/sec\n", rate);
    }
    printf("  Total time: %ld seconds\n", totalElapsed);
    printf("\n");

    // Cleanup
    miner->doFreeMemory();
    delete miner;
    delete[] gTableXCPU;
    delete[] gTableYCPU;
    free(lowerPattern);

    printf("Mining stopped.\n");
    return 0;
}
