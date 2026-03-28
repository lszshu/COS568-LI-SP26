#!/bin/bash
# Lookup-heavy true hybrid specialization head-to-head runner.

#SBATCH --job-name=cos568_m3ths
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=02:30:00
#SBATCH --array=0-2
#SBATCH --output=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.out
#SBATCH --error=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.err
#SBATCH --mail-type=ALL
#SBATCH --mail-user=sl8808@princeton.edu

set -euo pipefail

REPO_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26"
RUN_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_lookup_truehybrid_specialized/${SLURM_ARRAY_JOB_ID}"
DATASETS=(fb_100M_public_uint64 books_100M_public_uint64 osmc_100M_public_uint64)

DATASET="${DATASETS[$SLURM_ARRAY_TASK_ID]}"

cd "$REPO_ROOT"
mkdir -p "$REPO_ROOT/jobs/logs" "$RUN_ROOT"

if [ ! -f "$REPO_ROOT/build/benchmark" ]; then
    echo "Missing build/benchmark. Run scripts/build_benchmark.sh first."
    exit 1
fi

export OMP_NUM_THREADS=1

echo "Host: $(hostname)"
echo "Start: $(date)"
echo "Run root: ${RUN_ROOT}"
echo "Dataset: ${DATASET}"

srun --cpu-bind=cores bash "$REPO_ROOT/scripts/run_benchmarks_milestone3_lookup_truehybrid_specialized.sh" \
    "$DATASET" "$RUN_ROOT"

echo "End: $(date)"
