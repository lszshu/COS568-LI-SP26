#!/usr/bin/env bash

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 <run-root>"
    exit 1
fi

RUN_ROOT="$1"

for DATASET in fb_100M_public_uint64 books_100M_public_uint64 osmc_100M_public_uint64
do
    for INSERT_RATIO in 0.100000 0.900000
    do
        bash ./scripts/run_benchmarks_milestone3_workload.sh "$DATASET" "$INSERT_RATIO" "$RUN_ROOT"
    done
done
