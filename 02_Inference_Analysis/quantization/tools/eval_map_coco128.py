#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# 文件功能：用 ultralytics 在 coco128 上评测 FP32 / INT8 ONNX 模型的 mAP，量化精度掉点。

from __future__ import annotations

import argparse
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Evaluate YOLOv8 ONNX models (FP32/INT8) mAP on coco128."
    )
    parser.add_argument(
        "--model",
        action="append",
        required=True,
        help="ONNX model path，可传多次（如 FP32 + INT8 MinMax + INT8 Entropy）。",
    )
    parser.add_argument("--data", default="coco128.yaml")
    parser.add_argument("--imgsz", type=int, default=640)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        from ultralytics import YOLO  # noqa: PLC0415 — 重依赖延迟到运行期导入
    except ModuleNotFoundError as exc:
        raise SystemExit("缺少 ultralytics。请先安装：pip install ultralytics") from exc

    rows: list[tuple[str, float, float]] = []
    for model_path in args.model:
        path = Path(model_path)
        if not path.is_file():
            raise SystemExit(f"模型文件不存在：{path}")
        model = YOLO(str(path), task="detect")
        metrics = model.val(data=args.data, imgsz=args.imgsz, device="cpu")
        rows.append((path.name, metrics.box.map50, metrics.box.map))

    print("\n| 模型 | mAP50 | mAP50-95 |")
    print("|---|---:|---:|")
    baseline = rows[0][2]
    for name, map50, map5095 in rows:
        delta = "" if name == rows[0][0] else f"（掉点 {baseline - map5095:+.4f}）"
        print(f"| {name} | {map50:.4f} | {map5095:.4f}{delta} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
