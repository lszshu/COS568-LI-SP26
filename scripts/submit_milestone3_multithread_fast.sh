#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOB_SCRIPT="$REPO_ROOT/jobs/run_milestone3_multithread_fast_array.slurm"
RUN_ROOT_BASE="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread_fast"

THREAD_COUNT="${1:-8}"
ARRAY_SPEC="${2:-0-5}"
CPUS_PER_TASK="${CPUS_PER_TASK:-$THREAD_COUNT}"
TIME_LIMIT="${TIME_LIMIT:-01:00:00}"
MEMORY="${MEMORY:-48G}"
QOS="${QOS:-short}"

if [ ! -f "$JOB_SCRIPT" ]; then
    echo "Missing job script: $JOB_SCRIPT"
    exit 1
fi

SBATCH_OUTPUT="$(sbatch \
    --qos="$QOS" \
    --array="$ARRAY_SPEC" \
    --cpus-per-task="$CPUS_PER_TASK" \
    --time="$TIME_LIMIT" \
    --mem="$MEMORY" \
    --export=THREAD_COUNT="$THREAD_COUNT" \
    "$JOB_SCRIPT")"

JOB_ID="$(awk '{print $4}' <<<"$SBATCH_OUTPUT")"

echo "$SBATCH_OUTPUT"
echo "Run root: ${RUN_ROOT_BASE}/${JOB_ID}"
echo "Analyze with:"
echo "  $REPO_ROOT/scripts/analyze_milestone3_multithread_fast.sh ${JOB_ID} ${THREAD_COUNT}"
