#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/transfer2/T2_HASHES | shasum -a 256 -c --quiet - && echo "the transfer-2 builder and plan are unchanged since the freeze" || { echo "TRANSFER2 FILE CHANGED"; exit 1; }
