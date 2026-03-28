#!/usr/bin/env python3

from __future__ import annotations

import sys
from pathlib import Path

import pandas as pd


DATASETS = [
    "fb_100M_public_uint64",
    "books_100M_public_uint64",
    "osmc_100M_public_uint64",
]
INSERT_RATIO = "0.900000"


def avg_throughput(df: pd.DataFrame) -> pd.Series:
    return df[[
        "mixed_throughput_mops1",
        "mixed_throughput_mops2",
        "mixed_throughput_mops3",
    ]].mean(axis=1)


def load_result_csv(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    if "mixed_throughput_mops1" in df.columns:
        return df

    cols = [
        "index_name",
        "build_time_ns1",
        "build_time_ns2",
        "build_time_ns3",
        "index_size_bytes",
        "mixed_throughput_mops1",
        "mixed_throughput_mops2",
        "mixed_throughput_mops3",
        "search_method",
        "value",
    ]
    return pd.read_csv(path, header=None, names=cols)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} <run-root>", file=sys.stderr)
        return 1

    run_root = Path(sys.argv[1])
    rows = []

    for dataset in DATASETS:
        csv_path = (
            run_root
            / dataset
            / INSERT_RATIO
            / "results"
            / f"{dataset}_ops_2M_0.000000rq_0.500000nl_{INSERT_RATIO}i_0m_mix_results_table.csv"
        )
        df = load_result_csv(csv_path)
        df["avg_mops"] = avg_throughput(df)

        lipp = df[df["index_name"] == "LIPP"].sort_values("avg_mops", ascending=False).iloc[0]
        dpgm = df[df["index_name"] == "DynamicPGM"].sort_values("avg_mops", ascending=False).iloc[0]
        hybrid = df[df["index_name"] == "HybridPGMLIPPSpecialized"].sort_values(
            "avg_mops", ascending=False
        ).iloc[0]

        rows.append(
            {
                "dataset": dataset,
                "lipp_avg_mops": lipp["avg_mops"],
                "dpgm_avg_mops": dpgm["avg_mops"],
                "dpgm_search_method": dpgm.get("search_method", ""),
                "dpgm_value": dpgm.get("value", ""),
                "hybrid_avg_mops": hybrid["avg_mops"],
                "hybrid_search_method": hybrid.get("search_method", ""),
                "hybrid_value": hybrid.get("value", ""),
                "hybrid_minus_dpgm_mops": hybrid["avg_mops"] - dpgm["avg_mops"],
                "hybrid_minus_lipp_mops": hybrid["avg_mops"] - lipp["avg_mops"],
            }
        )

    summary = pd.DataFrame(rows)
    analysis_dir = run_root / "analysis"
    analysis_dir.mkdir(parents=True, exist_ok=True)
    out_path = analysis_dir / "milestone3_insert_tuned_summary.csv"
    summary.to_csv(out_path, index=False)
    print(out_path)
    print(summary.to_string(index=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
