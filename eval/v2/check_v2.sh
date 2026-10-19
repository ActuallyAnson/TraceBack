#!/bin/sh
# Fails if analyzer v2, the v2 prompts, the reasoner or the set generators changed since the v2 freeze.
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/v2/V2_HASHES | shasum -a 256 -c --quiet - && echo "v2 sources and prompts are unchanged since the v2 freeze" || { echo "V2 FILE CHANGED"; exit 1; }
