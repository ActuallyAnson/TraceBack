#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/combo/COMBO_HASHES | shasum -a 256 -c --quiet - && echo "combination rule, inputs and plan are unchanged since the freeze" || { echo "COMBO FILE CHANGED"; exit 1; }
