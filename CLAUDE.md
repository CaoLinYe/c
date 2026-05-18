# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

No build system — each `.c` file is a standalone program. Compile and run individually:

```bash
# XOR encrypt/decrypt tool
gcc -o encode/encode encode/encode.c && ./encode/encode <input> <output> <password>

# PNG template matching tool (requires libpng)
gcc -o template_match/template_match template_match/template_match.c -lpng && ./template_match/template_match <large.png> <small.png>

# Uppercase string converter
gcc -o upper-chars upper-chars.c && ./upper-chars "some string"
```

## Project Overview

Three independent C utilities, no shared code between them:

- **`encode/encode.c`** — XOR-based file encryption/decryption. Reads input file, XORs each byte with a repeating password key, writes to output file. Using the same password twice decrypts. Handles same-file input/output via temp file.
- **`template_match/template_match.c`** — Finds a small PNG image within a larger PNG image by exact pixel matching (RGBA). Depends on libpng. Converts all input formats to 8-bit RGBA for comparison. Uses brute-force sliding window search.
- **`upper-chars.c`** — Simple CLI that takes a string argument, converts lowercase letters to uppercase, prints result to stdout.

## Dependencies

- `libpng-dev` (for `template_match`)
