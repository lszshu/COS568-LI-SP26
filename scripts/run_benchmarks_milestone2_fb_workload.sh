#!/usr/bin/env bash

set -euo pipefail

BENCHMARK=build/benchmark
DATASET=fb_100M_public_uint64

if [ ! -f "$BENCHMARK" ]; then
    echo "benchmark binary does not exist"
    exit 1
fi

if [ $# -ne 1 ]; then
    echo "Usage: $0 <insert-ratio: 0.100000|0.900000>"
    exit 1
fi

case "$1" in
    0.1|0.100000)
        INSERT_RATIO="0.100000"
        ;;
    0.9|0.900000)
        INSERT_RATIO="0.900000"
        ;;
    *)
        echo "Unsupported insert ratio: $1"
        exit 1
        ;;
esac

OPS="./data/${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_mix"
RESULT_FILE="./results/${DATASET}_ops_2M_0.000000rq_0.500000nl_${INSERT_RATIO}i_0m_mix_results_table.csv"

mkdir -p ./results
rm -f "$RESULT_FILE"

for INDEX in LIPP DynamicPGM HybridPGMLIPP
do
    echo "Executing ${DATASET}, insert-ratio=${INSERT_RATIO}, index=${INDEX}"
    "$BENCHMARK" "./data/${DATASET}" "$OPS" --through --csv --only "$INDEX" -r 3
done

if [ -f "$RESULT_FILE" ]; then
    if head -n 1 "$RESULT_FILE" | grep -q "index_name"; then
        sed -i '1d' "$RESULT_FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$RESULT_FILE"
fi
