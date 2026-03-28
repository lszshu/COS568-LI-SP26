#!/bin/bash
#SBATCH --job-name=cos568_m3isht
#SBATCH --output=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/cos568_m3isht-%A_%a.out
#SBATCH --error=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26/jobs/logs/cos568_m3isht-%A_%a.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=32G
#SBATCH --time=04:00:00
#SBATCH --partition=cpu
#SBATCH --mail-type=BEGIN,END,FAIL
#SBATCH --mail-user=shuzhen@princeton.edu
#SBATCH --array=0-2

set -euo pipefail

REPO_ROOT=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26
RUN_ROOT=/scratch/gpfs/MENGDIW/shuzhen/COS568-LI-SP26-runs/milestone3_insert_sharded_tuned_specialized/${SLURM_ARRAY_JOB_ID}

mkdir -p "${RUN_ROOT}"

DATASETS=(
  fb_100M_public_uint64
  books_100M_public_uint64
  osmc_100M_public_uint64
)

DATASET="${DATASETS[$SLURM_ARRAY_TASK_ID]}"

cd "${REPO_ROOT}"
bash scripts/run_benchmarks_milestone3_insert_sharded_tuned_specialized.sh "${DATASET}" "${RUN_ROOT}"
