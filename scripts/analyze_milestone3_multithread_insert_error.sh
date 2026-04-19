#!/usr/bin/env bash

set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <job-id> [thread-count]"
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_ROOT_BASE="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_insert_error"
JOB_ID="$1"
THREAD_COUNT="${2:-8}"
RUN_ROOT="${RUN_ROOT_BASE}/${JOB_ID}"
SUMMARY_CSV="${RUN_ROOT}/analysis/milestone3_multithread_fast_summary.csv"

python "$REPO_ROOT/scripts/analysis_milestone3_multithread_fast.py" \
    "$RUN_ROOT" --threads "$THREAD_COUNT"

echo "Summary CSV: $SUMMARY_CSV"
python - "$SUMMARY_CSV" <<'PY'
import sys
import pandas as pd

summary = pd.read_csv(sys.argv[1])
summary = summary[summary["workload"] == "Mixed (10% lookup, 90% insert)"]
cols = [
    "dataset",
    "mt_hybrid_mops",
    "mt_hybrid_median_mops",
    "mt_hybrid_variant",
    "mt_hybrid_minus_single_lipp",
]
print(summary[cols].to_string(index=False))
PY
