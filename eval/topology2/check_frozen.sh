#!/bin/sh
# Fails if the analyzer or any prompt differs from the hashes recorded before topology-2 work began.
cd "$(dirname "$0")/../.." || exit 2
{ shasum -a 256 src/analyzer/*.cpp include/tb/analyzer.hpp prompts/*.txt; } > /tmp/tb_frozen_now.txt
grep -v '^#' eval/topology2/FROZEN_HASHES | diff - /tmp/tb_frozen_now.txt > /dev/null && echo "frozen: analyzer and prompts are unchanged since topology-2 planning" || { echo "FROZEN FILES CHANGED"; grep -v '^#' eval/topology2/FROZEN_HASHES | diff - /tmp/tb_frozen_now.txt; exit 1; }
