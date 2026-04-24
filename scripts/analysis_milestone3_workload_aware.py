#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


DATASETS = ["fb", "books", "osmc"]
DATASET_NAMES = {
    "fb": "fb_100M_public_uint64",
    "books": "books_100M_public_uint64",
    "osmc": "osmc_100M_public_uint64",
}
WORKLOADS = [
    ("0.100000", "Mixed (90% lookup, 10% insert)"),
    ("0.900000", "Mixed (10% lookup, 90% insert)"),
]
INDEX_ORDER = [
    "DynamicPGM",
    "LIPP",
    "HybridPGMLIPPIncremental",
    "HybridPGMLIPPWorkloadAwareSpecialized",
]


FIXED_COLUMNS = [
    "index_name",
    "build_time_ns1",
    "build_time_ns2",
    "build_time_ns3",
    "index_size_bytes",
    "mixed_throughput_mops1",
    "mixed_throughput_mops2",
    "mixed_throughput_mops3",
]


def read_results_csv(csv_path: Path) -> pd.DataFrame:
    rows = []
    with csv_path.open(newline="") as handle:
        reader = csv.reader(handle)
        for row in reader:
            if not row:
                continue
            if row[0] == "index_name":
                continue
            if len(row) < len(FIXED_COLUMNS):
                print(f"Warning: skipping short row in {csv_path}: {row}")
                continue

            record = dict(zip(FIXED_COLUMNS, row[: len(FIXED_COLUMNS)]))
            variant_fields = row[len(FIXED_COLUMNS) :]
            record["search_method"] = variant_fields[0] if variant_fields else ""
            record["value"] = ":".join(variant_fields[1:]) if len(variant_fields) > 1 else ""
            rows.append(record)

    if not rows:
        raise ValueError(f"No usable rows found in {csv_path}")

    frame = pd.DataFrame(rows)
    numeric_columns = FIXED_COLUMNS[1:]
    for column in numeric_columns:
        frame[column] = pd.to_numeric(frame[column], errors="coerce")
    frame = frame.dropna(subset=numeric_columns)
    return frame


def load_summary(run_root: Path) -> pd.DataFrame:
    rows = []
    for dataset_key in DATASETS:
        dataset_name = DATASET_NAMES[dataset_key]
        for insert_ratio, workload_label in WORKLOADS:
            csv_path = (
                run_root
                / dataset_name
                / insert_ratio
                / "results"
                / f"{dataset_name}_ops_2M_0.000000rq_0.500000nl_{insert_ratio}i_0m_mix_results_table.csv"
            )
            if not csv_path.exists():
                print(f"Warning: missing result file: {csv_path}")
                continue

            try:
                frame = read_results_csv(csv_path)
            except Exception as exc:
                print(f"Warning: failed to read {csv_path}: {exc}")
                continue

            for index_name in INDEX_ORDER:
                index_rows = frame[frame["index_name"] == index_name].copy()
                if index_rows.empty:
                    print(f"Warning: missing rows for {index_name} in {csv_path}")
                    continue

                index_rows["avg_mixed_throughput_mops"] = index_rows[
                    ["mixed_throughput_mops1", "mixed_throughput_mops2", "mixed_throughput_mops3"]
                ].mean(axis=1)
                best_row = index_rows.loc[index_rows["avg_mixed_throughput_mops"].idxmax()]

                rows.append(
                    {
                        "dataset": dataset_key,
                        "workload": workload_label,
                        "index_name": index_name,
                        "avg_mixed_throughput_mops": best_row["avg_mixed_throughput_mops"],
                        "index_size_bytes": best_row["index_size_bytes"],
                        "search_method": best_row.get("search_method", ""),
                        "value": best_row.get("value", ""),
                    }
                )
    if not rows:
        raise ValueError(f"No usable results found under {run_root}")
    return pd.DataFrame(rows)


def plot_grid(summary: pd.DataFrame, metric: str, output_path: Path, ylabel: str):
    fig, axes = plt.subplots(2, 3, figsize=(16, 8))
    colors = ["#2b6cb0", "#c05621", "#718096", "#2f855a"]

    for row_idx, (_, workload_label) in enumerate(WORKLOADS):
        for col_idx, dataset_key in enumerate(DATASETS):
            ax = axes[row_idx][col_idx]
            subset = summary[
                (summary["dataset"] == dataset_key) & (summary["workload"] == workload_label)
            ].set_index("index_name").reindex(INDEX_ORDER)

            if subset[metric].isna().all():
                ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
                ax.set_xticks(range(len(INDEX_ORDER)))
                ax.set_xticklabels(INDEX_ORDER, rotation=20)
                ax.set_title(f"{dataset_key.upper()} | {workload_label}")
                ax.set_ylabel(ylabel)
                continue

            ax.bar(INDEX_ORDER, subset[metric].fillna(0), color=colors)
            ax.set_title(f"{dataset_key.upper()} | {workload_label}")
            ax.set_ylabel(ylabel)
            ax.tick_params(axis="x", rotation=20)

    plt.tight_layout()
    plt.savefig(output_path, dpi=300)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_root", type=Path)
    args = parser.parse_args()

    analysis_dir = args.run_root / "analysis"
    analysis_dir.mkdir(parents=True, exist_ok=True)

    summary = load_summary(args.run_root)
    summary.to_csv(analysis_dir / "milestone3_workload_aware_summary.csv", index=False)

    plot_grid(
        summary,
        metric="avg_mixed_throughput_mops",
        output_path=analysis_dir / "milestone3_workload_aware_throughput.png",
        ylabel="Throughput (Mops/s)",
    )
    plot_grid(
        summary,
        metric="index_size_bytes",
        output_path=analysis_dir / "milestone3_workload_aware_index_size.png",
        ylabel="Size (bytes)",
    )


if __name__ == "__main__":
    main()
