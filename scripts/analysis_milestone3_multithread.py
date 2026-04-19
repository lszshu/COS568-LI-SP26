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

BASE_COLUMNS = [
    "index_name",
    "build_time_ns1",
    "build_time_ns2",
    "build_time_ns3",
    "index_size_bytes",
    "mixed_throughput_mops1",
    "mixed_throughput_mops2",
    "mixed_throughput_mops3",
]
VARIANT_COLUMNS = ["variant_1", "variant_2", "variant_3", "variant_4"]
ALL_COLUMNS = BASE_COLUMNS + VARIANT_COLUMNS


def read_results_csv(csv_path: Path) -> pd.DataFrame:
    rows = []
    with csv_path.open(newline="") as handle:
        reader = csv.reader(handle)
        next(reader, None)
        for row in reader:
            if not row:
                continue
            padded = row + [""] * (len(ALL_COLUMNS) - len(row))
            rows.append(padded[: len(ALL_COLUMNS)])

    frame = pd.DataFrame(rows, columns=ALL_COLUMNS)
    numeric_cols = [
        "build_time_ns1",
        "build_time_ns2",
        "build_time_ns3",
        "index_size_bytes",
        "mixed_throughput_mops1",
        "mixed_throughput_mops2",
        "mixed_throughput_mops3",
    ]
    for col in numeric_cols:
        frame[col] = pd.to_numeric(frame[col], errors="coerce")
    return frame


def best_row(csv_path: Path, index_name: str):
    frame = read_results_csv(csv_path)
    index_rows = frame[frame["index_name"] == index_name].copy()
    if index_rows.empty:
        return None
    throughput_cols = [
        "mixed_throughput_mops1",
        "mixed_throughput_mops2",
        "mixed_throughput_mops3",
    ]
    index_rows["avg_mops"] = index_rows[
        throughput_cols
    ].mean(axis=1)
    index_rows["median_mops"] = index_rows[
        throughput_cols
    ].median(axis=1)
    best_idx = index_rows.sort_values(
        ["median_mops", "avg_mops"], ascending=False
    ).index[0]
    return index_rows.loc[best_idx]


def load_summary(run_root: Path, thread_count: int) -> pd.DataFrame:
    rows = []
    for dataset_key in DATASETS:
        dataset_name = DATASET_NAMES[dataset_key]
        for insert_ratio, workload_label in WORKLOADS:
            single_csv = (
                run_root
                / dataset_name
                / insert_ratio
                / "single"
                / "results"
                / f"{dataset_name}_ops_2M_0.000000rq_0.500000nl_{insert_ratio}i_0m_mix_results_table.csv"
            )
            multi_csv = (
                run_root
                / dataset_name
                / insert_ratio
                / f"{thread_count}t"
                / "results"
                / f"{dataset_name}_ops_2M_0.000000rq_0.500000nl_{insert_ratio}i_0m_{thread_count}t_mix_results_table.csv"
            )
            if not single_csv.exists() or not multi_csv.exists():
                continue

            lipp = best_row(single_csv, "LIPP")
            single_hybrid = best_row(single_csv, "HybridPGMLIPPWorkloadAwareSpecialized")
            lipp_threadsafe = best_row(multi_csv, "LIPPThreadSafe")
            mt_hybrid = best_row(multi_csv, "HybridPGMLIPPConcurrentWorkloadAware")
            if lipp is None or lipp_threadsafe is None or mt_hybrid is None:
                continue

            rows.append(
                {
                    "dataset": dataset_key,
                    "workload": workload_label,
                    "single_lipp_mops": lipp["avg_mops"],
                    "single_lipp_median_mops": lipp["median_mops"],
                    "single_lipp_variant": ":".join(
                        str(v)
                        for v in [lipp.get(col, "") for col in VARIANT_COLUMNS]
                        if str(v) not in {"", "nan"}
                    ),
                    "single_hybrid_mops": None if single_hybrid is None else single_hybrid["avg_mops"],
                    "single_hybrid_median_mops": None
                    if single_hybrid is None
                    else single_hybrid["median_mops"],
                    "single_hybrid_variant": ""
                    if single_hybrid is None
                    else ":".join(
                        str(v)
                        for v in [single_hybrid.get(col, "") for col in VARIANT_COLUMNS]
                        if str(v) not in {"", "nan"}
                    ),
                    "mt_lipp_threadsafe_mops": lipp_threadsafe["avg_mops"],
                    "mt_lipp_threadsafe_median_mops": lipp_threadsafe["median_mops"],
                    "mt_lipp_threadsafe_variant": ":".join(
                        str(v)
                        for v in [lipp_threadsafe.get(col, "") for col in VARIANT_COLUMNS]
                        if str(v) not in {"", "nan"}
                    ),
                    "mt_hybrid_mops": mt_hybrid["avg_mops"],
                    "mt_hybrid_median_mops": mt_hybrid["median_mops"],
                    "mt_hybrid_variant": ":".join(
                        str(v)
                        for v in [mt_hybrid.get(col, "") for col in VARIANT_COLUMNS]
                        if str(v) not in {"", "nan"}
                    ),
                    "mt_hybrid_size_bytes": mt_hybrid["index_size_bytes"],
                    "mt_hybrid_minus_single_lipp": mt_hybrid["avg_mops"] - lipp["avg_mops"],
                    "mt_hybrid_median_minus_single_lipp": mt_hybrid["median_mops"]
                    - lipp["median_mops"],
                    "mt_hybrid_minus_mt_lipp_threadsafe": mt_hybrid["avg_mops"]
                    - lipp_threadsafe["avg_mops"],
                    "mt_hybrid_median_minus_mt_lipp_threadsafe": mt_hybrid["median_mops"]
                    - lipp_threadsafe["median_mops"],
                }
            )
    if not rows:
        raise ValueError(f"No usable results found under {run_root}")
    return pd.DataFrame(rows)


def plot_summary(summary: pd.DataFrame, output_dir: Path):
    colors = ["#c05621", "#718096", "#2f855a"]
    fig, axes = plt.subplots(2, 3, figsize=(16, 8))

    for row_idx, (_, workload_label) in enumerate(WORKLOADS):
        for col_idx, dataset_key in enumerate(DATASETS):
            ax = axes[row_idx][col_idx]
            subset = summary[
                (summary["dataset"] == dataset_key) & (summary["workload"] == workload_label)
            ]
            if subset.empty:
                ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
                ax.set_title(f"{dataset_key.upper()} | {workload_label}")
                continue

            row = subset.iloc[0]
            labels = ["LIPP-1T", "LIPP-TS", "Hybrid-MT"]
            values = [
                row["single_lipp_mops"],
                row["mt_lipp_threadsafe_mops"],
                row["mt_hybrid_mops"],
            ]
            ax.bar(labels, values, color=colors)
            ax.set_title(f"{dataset_key.upper()} | {workload_label}")
            ax.set_ylabel("Throughput (Mops/s)")
            ax.tick_params(axis="x", rotation=15)

    plt.tight_layout()
    plt.savefig(output_dir / "milestone3_multithread_throughput.png", dpi=300)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_root", type=Path)
    parser.add_argument("--threads", type=int, required=True)
    args = parser.parse_args()

    analysis_dir = args.run_root / "analysis"
    analysis_dir.mkdir(parents=True, exist_ok=True)

    summary = load_summary(args.run_root, args.threads)
    summary.to_csv(analysis_dir / "milestone3_multithread_summary.csv", index=False)
    plot_summary(summary, analysis_dir)


if __name__ == "__main__":
    main()
