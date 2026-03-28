#!/usr/bin/env bash

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 <dataset> <run-root>"
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARK="$REPO_ROOT/build/benchmark"
DATASET="$1"
RUN_ROOT="$2"
INSERT_RATIO="0.100000"

if [ ! -f "$BENCHMARK" ]; then
    echo "Missing benchmark binary: $BENCHMARK"
    exit 1
fi

DATA_FILE="$REPO_ROOT/data/$DATASET"
OPS_FILE="$REPO_ROOT/data/${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_mix"
WORK_DIR="$RUN_ROOT/$DATASET/$INSERT_RATIO"
RESULT_FILE="$WORK_DIR/results/${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_mix_results_table.csv"

mkdir -p "$WORK_DIR/results"
rm -f "$RESULT_FILE"

cd "$WORK_DIR"

echo "Executing dataset=${DATASET}, insert-ratio=${INSERT_RATIO}, index=LIPP"
"$BENCHMARK" "$DATA_FILE" "$OPS_FILE" --through --csv --only LIPP -r 3

echo "Executing dataset=${DATASET}, insert-ratio=${INSERT_RATIO}, index=HybridPGMLIPPIncremental, params=131072,256,0,0,0,0,0,1"
"$BENCHMARK" "$DATA_FILE" "$OPS_FILE" --through --csv --only HybridPGMLIPPIncremental -r 3 \
    --params 131072,256,0,0,0,0,0,1

echo "Executing dataset=${DATASET}, insert-ratio=${INSERT_RATIO}, index=HybridPGMLIPPDirectLippSpecialized"
"$BENCHMARK" "$DATA_FILE" "$OPS_FILE" --through --csv --only HybridPGMLIPPDirectLippSpecialized -r 3

if [ -f "$RESULT_FILE" ]; then
    if head -n 1 "$RESULT_FILE" | grep -q "index_name"; then
        sed -i '1d' "$RESULT_FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$RESULT_FILE"
fi
