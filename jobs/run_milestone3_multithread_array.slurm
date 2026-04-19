#!/bin/bash

#SBATCH --job-name=cos568_m3mt
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=64G
#SBATCH --time=04:00:00
#SBATCH --array=0-5
#SBATCH --output=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.out
#SBATCH --error=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.err

set -euo pipefail

REPO_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26"
RUN_ROOT_BASE="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_multithread"
THREAD_COUNT="${THREAD_COUNT:-8}"
DATASETS=(fb_100M_public_uint64 books_100M_public_uint64 osmc_100M_public_uint64)
INSERT_RATIOS=(0.100000 0.900000)

DATASET_INDEX=$((SLURM_ARRAY_TASK_ID % 3))
RATIO_INDEX=$((SLURM_ARRAY_TASK_ID / 3))

DATASET="${DATASETS[$DATASET_INDEX]}"
INSERT_RATIO="${INSERT_RATIOS[$RATIO_INDEX]}"
RUN_ROOT="${RUN_ROOT_BASE}/${SLURM_ARRAY_JOB_ID}"

cd "$REPO_ROOT"
mkdir -p "$REPO_ROOT/jobs/logs" "$RUN_ROOT"

if [ ! -f "$REPO_ROOT/build/benchmark" ] || [ ! -f "$REPO_ROOT/build/generate" ]; then
    echo "Missing build artifacts. Run scripts/build_benchmark.sh first."
    exit 1
fi

export OMP_NUM_THREADS=1

echo "Host: $(hostname)"
echo "Start: $(date)"
echo "Run root: ${RUN_ROOT}"
echo "Dataset: ${DATASET}"
echo "Insert ratio: ${INSERT_RATIO}"
echo "Thread count: ${THREAD_COUNT}"

/bin/bash "$REPO_ROOT/scripts/run_benchmarks_milestone3_multithread_workload.sh" \
    "$DATASET" "$INSERT_RATIO" "$THREAD_COUNT" "$RUN_ROOT"

echo "End: $(date)"
