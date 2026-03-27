from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


RESULTS_DIR = Path("results")
OUTPUT_DIR = Path("analysis_results")
DATASET = "fb_100M_public_uint64"
INDEX_ORDER = ["DynamicPGM", "LIPP", "HybridPGMLIPP"]
WORKLOADS = [
    ("0.100000", "Mixed (90% lookup, 10% insert)"),
    ("0.900000", "Mixed (10% lookup, 90% insert)"),
]


def load_best_rows():
    rows = []
    for insert_ratio, workload_label in WORKLOADS:
        csv_path = RESULTS_DIR / f"{DATASET}_ops_2M_0.000000rq_0.500000nl_{insert_ratio}i_0m_mix_results_table.csv"
        if not csv_path.exists():
            raise FileNotFoundError(f"Missing result file: {csv_path}")

        frame = pd.read_csv(csv_path)
        for index_name in INDEX_ORDER:
            index_rows = frame[frame["index_name"] == index_name].copy()
            if index_rows.empty:
                raise ValueError(f"Missing rows for {index_name} in {csv_path}")

            index_rows["avg_mixed_throughput_mops"] = index_rows[
                ["mixed_throughput_mops1", "mixed_throughput_mops2", "mixed_throughput_mops3"]
            ].mean(axis=1)
            best_row = index_rows.loc[index_rows["avg_mixed_throughput_mops"].idxmax()]
            rows.append(
                {
                    "workload": workload_label,
                    "index_name": index_name,
                    "avg_mixed_throughput_mops": best_row["avg_mixed_throughput_mops"],
                    "index_size_bytes": best_row["index_size_bytes"],
                    "search_method": best_row.get("search_method", ""),
                    "value": best_row.get("value", ""),
                }
            )
    return pd.DataFrame(rows)


def plot_summary(summary: pd.DataFrame):
    OUTPUT_DIR.mkdir(exist_ok=True)
    summary.to_csv(OUTPUT_DIR / "milestone2_fb_summary.csv", index=False)

    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    axes = axes.flatten()
    colors = ["#2b6cb0", "#c05621", "#2f855a"]

    for idx, (_, workload_label) in enumerate(WORKLOADS):
        workload_rows = summary[summary["workload"] == workload_label].set_index("index_name").loc[INDEX_ORDER]

        ax_t = axes[idx]
        ax_t.bar(INDEX_ORDER, workload_rows["avg_mixed_throughput_mops"], color=colors)
        ax_t.set_title(f"{workload_label} Throughput")
        ax_t.set_ylabel("Throughput (Mops/s)")
        ax_t.tick_params(axis="x", rotation=15)

        ax_s = axes[idx + 2]
        ax_s.bar(INDEX_ORDER, workload_rows["index_size_bytes"], color=colors)
        ax_s.set_title(f"{workload_label} Index Size")
        ax_s.set_ylabel("Size (bytes)")
        ax_s.tick_params(axis="x", rotation=15)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "milestone2_fb_plots.png", dpi=300)


if __name__ == "__main__":
    plot_summary(load_best_rows())
