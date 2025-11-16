# Search Modes: Random vs Sequential

## Overview

Rummage supports two search modes: random (default) and sequential. Each has different trade-offs for speed, reliability, and guarantees.

## Random Mode (Default)

Generates random private keys and checks if they match your pattern.

**Advantages:**
- Faster for short patterns (no checkpoint overhead)
- Simple and straightforward

**Disadvantages:**
- No resume capability - crash means starting over
- No progress tracking - unknown completion time
- Probabilistic - not guaranteed to find a match
- Duplicate work - wastes ~37% of effort after exhausting search space once

**Use when:**
- Pattern is short (< 1 hour to find)
- You don't need to resume
- Speed matters more than guarantees

## Sequential Mode

Exhaustively searches the keyspace from a random 256-bit starting offset.

**Advantages:**
- Resume capability - saves checkpoint every 60 seconds
- Progress tracking - know exactly how far along you are
- Exhaustive search - checks every key in range exactly once
- No wasted work - never checks same key twice
- No cuRAND overhead - deterministic key generation

**Disadvantages:**
- Checkpoint file contains sensitive offset (must be protected)
- Slightly slower than random for very short patterns

**Use when:**
- Pattern requires > 1 hour of searching
- You need resume capability
- You want exhaustive search of the space
- You want to know progress and ETA

## Search Space Example

For a 9-character bech32 prefix like "satoshi00":

**Total search space:**
- 9 characters × 5 bits/char = 45 bits
- 2^45 = 35,184,372,088,832 keys (35.18 trillion)

**Random mode probabilities:**
- 50% chance: 24.4 trillion keys
- 63% chance: 35.18 trillion keys (full space)
- 95% chance: 105.5 trillion keys (3x full space)
- Never guaranteed

**Sequential mode:**
- Exhausts search space in: 35.18 trillion keys (2^45)
- At 40M keys/sec: ~10.2 days
- Resumable at any point
- Note: Finding a match assumes uniform distribution of valid keys

## Checkpoint Security

Sequential mode saves state to a checkpoint file (default: `checkpoint.txt`).

**The checkpoint contains:**
- Current iteration number
- Keys generated count
- Starting offset (256-bit number)

**Security considerations:**

The 256-bit starting offset must be kept secret. If someone obtains:
1. Your checkpoint file
2. Your target npub address

They can calculate your private key by:
```
private_key = starting_offset + current_iteration
```

**Protect the checkpoint:**
```bash
chmod 600 checkpoint.txt  # Set by default
```

**Never commit checkpoint files to git** - already in `.gitignore`.

## Probability Formulas

**Random mode probability of finding match:**
```
P(found) = 1 - e^(-n/N)

Where:
  n = number of keys tried
  N = total search space (2^bits)
  e = Euler's number (2.71828...)
```

**Example for 9-char pattern (N = 2^45):**
- After 35.18T keys: P = 1 - e^(-1) = 63%
- After 105.5T keys: P = 1 - e^(-3) = 95%
- After 161.8T keys: P = 1 - e^(-4.6) = 99%

**Sequential mode:**
```
Exhausts entire search space after N keys
Expected matches = 1 (assuming uniform distribution)
Not guaranteed - depends on key distribution
```

## Examples

**Quick search:**
```bash
./rummage --npub-prefix test
# Random mode, should find match in seconds
```

**Long search with resume:**
```bash
./rummage --npub-prefix satoshi00 --sequential
# Sequential mode, ~10 days, resumable
```

**Resume from checkpoint:**
```bash
./rummage --npub-prefix satoshi00 --sequential
# Automatically resumes from checkpoint.txt if it exists
```

## Choosing a Mode

| Criterion | Random | Sequential |
|-----------|--------|------------|
| Expected time < 1 hour | ✓ | |
| Expected time > 1 hour | | ✓ |
| Need resume capability | | ✓ |
| Need progress tracking | | ✓ |
| Need exhaustive search | | ✓ |
| Maximum throughput | ✓ | |
| Can't protect checkpoint file | ✓ | |

**Simple rule:** Use sequential for any search longer than 1 hour.
