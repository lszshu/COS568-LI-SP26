#!/usr/bin/env bash

set -euo pipefail

if [ $# -ne 4 ]; then
    echo "Usage: $0 <dataset> <insert-ratio> <thread-count> <run-root>"
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARK="$REPO_ROOT/build/benchmark"
GENERATE="$REPO_ROOT/build/generate"
DATASET="$1"
INSERT_RATIO="$2"
THREAD_COUNT="$3"
RUN_ROOT="$4"

case "$INSERT_RATIO" in
    0.1|0.100000)
        INSERT_RATIO="0.100000"
        ;;
    0.9|0.900000)
        INSERT_RATIO="0.900000"
        ;;
    *)
        echo "Unsupported insert ratio: $INSERT_RATIO"
        exit 1
        ;;
esac

if [ ! -f "$BENCHMARK" ] || [ ! -f "$GENERATE" ]; then
    echo "Missing benchmark or generate binary under $REPO_ROOT/build"
    exit 1
fi

DATA_FILE="$REPO_ROOT/data/$DATASET"
ST_OPS_FILE="$REPO_ROOT/data/${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_mix"
MT_OPS_FILE="$REPO_ROOT/data/${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_${THREAD_COUNT}t_mix"
BASE_FILE="${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_mix_results_table.csv"
MT_BASE_FILE="${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_${THREAD_COUNT}t_mix_results_table.csv"
WORK_DIR="$RUN_ROOT/$DATASET/$INSERT_RATIO"
SINGLE_DIR="$WORK_DIR/single"
MULTI_DIR="$WORK_DIR/${THREAD_COUNT}t"
BIN_DIR="$WORK_DIR/bin"
BENCHMARK_COPY="$BIN_DIR/benchmark"

mkdir -p "$BIN_DIR"
cp -f "$BENCHMARK" "$BENCHMARK_COPY"

generate_if_missing() {
    local ops_file="$1"
    shift
    if [ -f "$ops_file" ]; then
        return
    fi
    echo "Generating workload: $ops_file"
    (
        cd "$REPO_ROOT/build"
        ./generate "../data/$DATASET" 2000000 \
            --insert-ratio "$INSERT_RATIO" \
            --negative-lookup-ratio 0.5 \
            --mix "$@"
    )
}

add_csv_header() {
    local result_file="$1"
    if [ ! -f "$result_file" ]; then
        return
    fi
    if head -n 1 "$result_file" | grep -q "index_name"; then
        sed -i '1d' "$result_file"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,variant_1,variant_2,variant_3,variant_4\n/' "$result_file"
}

generate_if_missing "$ST_OPS_FILE"
generate_if_missing "$MT_OPS_FILE" --thread "$THREAD_COUNT"

mkdir -p "$SINGLE_DIR/results" "$MULTI_DIR/results"
rm -f "$SINGLE_DIR/results/$BASE_FILE" "$MULTI_DIR/results/$MT_BASE_FILE"

cd "$SINGLE_DIR"
for INDEX in LIPP HybridPGMLIPPWorkloadAwareSpecialized
do
    echo "Executing single-thread dataset=${DATASET}, insert-ratio=${INSERT_RATIO}, index=${INDEX}"
    "$BENCHMARK_COPY" "$DATA_FILE" "$ST_OPS_FILE" --through --csv --only "$INDEX" -r 3
done
add_csv_header "$SINGLE_DIR/results/$BASE_FILE"

cd "$MULTI_DIR"
for INDEX in LIPPThreadSafe HybridPGMLIPPConcurrentWorkloadAware
do
    echo "Executing multithread dataset=${DATASET}, insert-ratio=${INSERT_RATIO}, threads=${THREAD_COUNT}, index=${INDEX}"
    "$BENCHMARK_COPY" "$DATA_FILE" "$MT_OPS_FILE" --through --csv --only "$INDEX" --threads "$THREAD_COUNT" -r 3
done
add_csv_header "$MULTI_DIR/results/$MT_BASE_FILE"
