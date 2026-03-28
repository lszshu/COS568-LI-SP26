#!/usr/bin/env python3

from __future__ import annotations

import csv
import sys
from pathlib import Path


DATASETS = [
    "fb_100M_public_uint64",
    "books_100M_public_uint64",
    "osmc_100M_public_uint64",
]
INSERT_RATIO = "0.900000"


def load_rows(path: Path) -> list[list[str]]:
    with path.open() as f:
        rows = list(csv.reader(f))
    if rows and rows[0] and rows[0][0] == "index_name":
        rows = rows[1:]
    return rows


def avg_mops(row: list[str]) -> float:
    return sum(float(row[i]) for i in (5, 6, 7)) / 3.0


def variant(row: list[str]) -> str:
    return ":".join(row[8:]) if len(row) > 8 else ""


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} <run-root>", file=sys.stderr)
        return 1

    run_root = Path(sys.argv[1])
    summary_rows: list[dict[str, object]] = []

    for dataset in DATASETS:
        csv_path = (
            run_root
            / dataset
            / INSERT_RATIO
            / "results"
            / f"{dataset}_ops_2M_0.000000rq_0.500000nl_{INSERT_RATIO}i_0m_mix_results_table.csv"
        )
        if not csv_path.exists():
            summary_rows.append(
                {
                    "dataset": dataset,
                    "status": "missing",
                    "lipp_avg_mops": "",
                    "dpgm_avg_mops": "",
                    "dpgm_variant": "",
                    "hybrid_avg_mops": "",
                    "hybrid_variant": "",
                    "hybrid_minus_dpgm_mops": "",
                    "hybrid_minus_lipp_mops": "",
                }
            )
            continue

        rows = load_rows(csv_path)
        if not rows:
            summary_rows.append(
                {
                    "dataset": dataset,
                    "status": "empty",
                    "lipp_avg_mops": "",
                    "dpgm_avg_mops": "",
                    "dpgm_variant": "",
                    "hybrid_avg_mops": "",
                    "hybrid_variant": "",
                    "hybrid_minus_dpgm_mops": "",
                    "hybrid_minus_lipp_mops": "",
                }
            )
            continue

        lipp_rows = [row for row in rows if row[0] == "LIPP"]
        dpgm_rows = [row for row in rows if row[0] == "DynamicPGM"]
        hybrid_rows = [row for row in rows if row[0] == "HybridPGMLIPPSpecialized"]

        lipp = max(lipp_rows, key=avg_mops) if lipp_rows else None
        dpgm = max(dpgm_rows, key=avg_mops) if dpgm_rows else None
        hybrid = max(hybrid_rows, key=avg_mops) if hybrid_rows else None

        summary_rows.append(
            {
                "dataset": dataset,
                "status": "ok",
                "lipp_avg_mops": avg_mops(lipp) if lipp else "",
                "dpgm_avg_mops": avg_mops(dpgm) if dpgm else "",
                "dpgm_variant": variant(dpgm) if dpgm else "",
                "hybrid_avg_mops": avg_mops(hybrid) if hybrid else "",
                "hybrid_variant": variant(hybrid) if hybrid else "",
                "hybrid_minus_dpgm_mops": avg_mops(hybrid) - avg_mops(dpgm)
                if dpgm and hybrid
                else "",
                "hybrid_minus_lipp_mops": avg_mops(hybrid) - avg_mops(lipp)
                if lipp and hybrid
                else "",
            }
        )

    analysis_dir = run_root / "analysis"
    analysis_dir.mkdir(parents=True, exist_ok=True)
    out_path = analysis_dir / "milestone3_insert_sharded_summary.csv"

    with out_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "dataset",
                "status",
                "lipp_avg_mops",
                "dpgm_avg_mops",
                "dpgm_variant",
                "hybrid_avg_mops",
                "hybrid_variant",
                "hybrid_minus_dpgm_mops",
                "hybrid_minus_lipp_mops",
            ],
        )
        writer.writeheader()
        writer.writerows(summary_rows)

    print(out_path)
    for row in summary_rows:
        print(row)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
