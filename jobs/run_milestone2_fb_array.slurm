#!/bin/bash
# Run Milestone 2 Facebook mixed-workload benchmarks in parallel by workload.
# Each array task remains single-threaded at the benchmark level, so the
# experiment semantics match the single-CPU version.

#SBATCH --job-name=cos568_m2_fb
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=04:00:00
#SBATCH --array=0-1
#SBATCH --output=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.out
#SBATCH --error=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%A_%a.err
#SBATCH --mail-type=ALL
#SBATCH --mail-user=sl8808@princeton.edu

# Uncomment if you want strict node exclusivity for cleaner timing.
##SBATCH --exclusive

set -euo pipefail

REPO_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26"
INSERT_RATIOS=(0.100000 0.900000)

cd "$REPO_ROOT"
mkdir -p "$REPO_ROOT/jobs/logs"

if [ ! -f "$REPO_ROOT/build/benchmark" ]; then
    echo "Missing build/benchmark. Run scripts/build_benchmark.sh first."
    exit 1
fi

export OMP_NUM_THREADS=1

INSERT_RATIO="${INSERT_RATIOS[$SLURM_ARRAY_TASK_ID]}"

echo "Host: $(hostname)"
echo "Start: $(date)"
echo "Insert ratio: ${INSERT_RATIO}"

srun --cpu-bind=cores bash "$REPO_ROOT/scripts/run_benchmarks_milestone2_fb_workload.sh" "$INSERT_RATIO"

echo "End: $(date)"
