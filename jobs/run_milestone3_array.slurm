#!/bin/bash
# Isolated Milestone 3 experiment runner.
# Results are written outside the repository results/ directory so failed
# experiments do not pollute the milestone1/milestone2 outputs.

#SBATCH --job-name=cos568_m3
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=06:00:00
#SBATCH --array=0-5
#SBATCH --output=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.out
#SBATCH --error=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.err
#SBATCH --mail-type=ALL
#SBATCH --mail-user=sl8808@princeton.edu

# Uncomment for stricter timing isolation if queue time is acceptable.
##SBATCH --exclusive

set -euo pipefail

REPO_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26"
RUN_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3/${SLURM_ARRAY_JOB_ID}"
DATASETS=(fb_100M_public_uint64 books_100M_public_uint64 osmc_100M_public_uint64)
INSERT_RATIOS=(0.100000 0.900000)

TASK_ID="${SLURM_ARRAY_TASK_ID}"
DATASET_INDEX=$((TASK_ID / 2))
RATIO_INDEX=$((TASK_ID % 2))

DATASET="${DATASETS[$DATASET_INDEX]}"
INSERT_RATIO="${INSERT_RATIOS[$RATIO_INDEX]}"

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
echo "Insert ratio: ${INSERT_RATIO}"

srun --cpu-bind=cores bash "$REPO_ROOT/scripts/run_benchmarks_milestone3_workload.sh" "$DATASET" "$INSERT_RATIO" "$RUN_ROOT"

echo "End: $(date)"
