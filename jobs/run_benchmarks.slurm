#!/bin/bash
# Slurm job for Milestone 1 benchmark runs.
# This assumes you have already finished:
# 1) download_dataset.sh
# 2) create_minimal_cmake.sh
# 3) generate_workloads.sh
# 4) build_benchmark.sh

#SBATCH --job-name=cos568_bench
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=04:00:00
#SBATCH --output=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%j.out
#SBATCH --error=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/%x-%j.err
#SBATCH --mail-type=ALL
#SBATCH --mail-user=sl8808@princeton.edu

# If you want more stable timing and can tolerate longer queue time,
# uncomment the next line to request the whole node.
##SBATCH --exclusive

set -euo pipefail

REPO_ROOT="/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26"

cd "$REPO_ROOT"

if [ ! -f "$REPO_ROOT/build/benchmark" ]; then
    echo "Missing build/benchmark. Run scripts/build_benchmark.sh first."
    exit 1
fi

mkdir -p "$REPO_ROOT/jobs/logs"
mkdir -p "$REPO_ROOT/results"

# Keep the benchmark single-threaded unless you explicitly change the workload script.
export OMP_NUM_THREADS=1

echo "Host: $(hostname)"
echo "Start: $(date)"
echo "Repo: $REPO_ROOT"

srun --cpu-bind=cores bash "$REPO_ROOT/scripts/run_benchmarks.sh"

echo "End: $(date)"
