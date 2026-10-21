#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/transfer/TRANSFER_HASHES | shasum -a 256 -c --quiet - && echo "hybrid 2, the transfer converter and the plan are unchanged since the freeze" || { echo "TRANSFER FILE CHANGED"; exit 1; }
