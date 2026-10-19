#!/bin/sh
# Fails if any v1 result, locked manifest, scoring protocol or frozen v1 source differs from the v1 freeze.
cd "$(dirname "$0")/../.." || exit 2
sh eval/topology2/check_frozen.sh >/dev/null || { echo "v1 analyzer or prompts changed"; exit 1; }
grep -v '^#' eval/v2/V1_HASHES | shasum -a 256 -c --quiet - && echo "v1 results, manifests and scoring protocol are unchanged since the v1 freeze" || { echo "V1 ARTIFACT CHANGED"; exit 1; }
