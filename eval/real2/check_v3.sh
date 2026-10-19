#!/bin/sh
# Fails if analyzer v3 or the converter changed since the v3 freeze (v1 and v2 have their own checks).
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/real2/V3_HASHES | shasum -a 256 -c --quiet - && echo "analyzer v3 and the converter are unchanged since the v3 freeze" || { echo "V3 FILE CHANGED"; exit 1; }
