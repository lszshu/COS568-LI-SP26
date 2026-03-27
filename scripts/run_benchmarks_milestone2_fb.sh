#!/usr/bin/env bash

set -euo pipefail

for INSERT_RATIO in 0.100000 0.900000
do
    bash ./scripts/run_benchmarks_milestone2_fb_workload.sh "$INSERT_RATIO"
done
