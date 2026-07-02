#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# 文件功能：用 ONNX Runtime static PTQ 为 YOLOv8n 生成 MinMax / Entropy INT8 QDQ 模型。

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable


IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate ORT static INT8 QDQ YOLOv8 models."
    )
    parser.add_argument("--model", required=True, help="FP32 ONNX model path.")
    parser.add_argument(
        "--calib-image",
        action="append",
        default=[],
        help="Calibration image path. Can be passed multiple times.",
    )
    parser.add_argument(
        "--calib-dir",
        action="append",
        default=[],
        help="Directory containing calibration images.",
    )
    parser.add_argument(
        "--output-dir",
        default="build/02_Inference_Analysis/quantization/models",
        help="Directory for preprocessed and INT8 ONNX outputs.",
    )
    parser.add_argument("--input-size", type=int, default=640)
    return parser.parse_args()


def import_runtime_deps():
    try:
        import cv2  # type: ignore
        import numpy as np  # type: ignore
        import onnxruntime as ort  # type: ignore
        from onnxruntime.quantization import (  # type: ignore
            CalibrationDataReader,
            CalibrationMethod,
            QuantFormat,
            QuantType,
            quant_pre_process,
            quantize_static,
        )
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "缺少 Python 量化依赖。请先安装："
            "pip install onnxruntime onnx opencv-python numpy"
        ) from exc

    return {
        "cv2": cv2,
        "np": np,
        "ort": ort,
        "CalibrationDataReader": CalibrationDataReader,
        "CalibrationMethod": CalibrationMethod,
        "QuantFormat": QuantFormat,
        "QuantType": QuantType,
        "quant_pre_process": quant_pre_process,
        "quantize_static": quantize_static,
    }


def collect_images(calib_images: Iterable[str], calib_dirs: Iterable[str]) -> list[Path]:
    images: list[Path] = []
    for item in calib_images:
        path = Path(item)
        if not path.is_file():
            raise SystemExit(f"校准图片不存在：{path}")
        images.append(path)

    for item in calib_dirs:
        root = Path(item)
        if not root.is_dir():
            raise SystemExit(f"校准目录不存在：{root}")
        for path in sorted(root.rglob("*")):
            if path.suffix.lower() in IMAGE_SUFFIXES and path.is_file():
                images.append(path)

    if not images:
        raise SystemExit("未提供校准图片。请传 --calib-image 或 --calib-dir。")
    return images


def letterbox_rgb(image_rgb, size: int, cv2, np):
    src_h, src_w = image_rgb.shape[:2]
    scale = min(size / src_w, size / src_h)
    new_w = int(round(src_w * scale))
    new_h = int(round(src_h * scale))
    resized = cv2.resize(image_rgb, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), 114, dtype=np.uint8)
    pad_left = (size - new_w) // 2
    pad_top = (size - new_h) // 2
    canvas[pad_top : pad_top + new_h, pad_left : pad_left + new_w, :] = resized
    return canvas


def preprocess_image(path: Path, size: int, cv2, np):
    bgr = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if bgr is None:
        raise RuntimeError(f"图片读取失败：{path}")
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    letterboxed = letterbox_rgb(rgb, size, cv2, np)
    tensor = letterboxed.astype(np.float32) / 255.0
    tensor = np.transpose(tensor, (2, 0, 1))
    return np.expand_dims(tensor, axis=0)


def make_reader_class(calibration_data_reader_base):
    class YoloCalibrationDataReader(calibration_data_reader_base):
        def __init__(self, input_name: str, image_paths: list[Path], size: int, cv2, np):
            self.input_name = input_name
            self.samples = [
                {input_name: preprocess_image(path, size, cv2, np)}
                for path in image_paths
            ]
            self.index = 0

        def get_next(self):
            if self.index >= len(self.samples):
                return None
            item = self.samples[self.index]
            self.index += 1
            return item

        def rewind(self):
            self.index = 0

    return YoloCalibrationDataReader


def main() -> int:
    args = parse_args()
    deps = import_runtime_deps()
    cv2 = deps["cv2"]
    np = deps["np"]
    ort = deps["ort"]
    calibration_data_reader_base = deps["CalibrationDataReader"]
    calibration_method = deps["CalibrationMethod"]
    quant_format = deps["QuantFormat"]
    quant_type = deps["QuantType"]
    quant_pre_process = deps["quant_pre_process"]
    quantize_static = deps["quantize_static"]

    model = Path(args.model)
    if not model.is_file():
        raise SystemExit(f"模型文件不存在：{model}")
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    images = collect_images(args.calib_image, args.calib_dir)

    session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name

    preprocessed_model = output_dir / f"{model.stem}.preprocessed.onnx"
    minmax_model = output_dir / f"{model.stem}.int8.minmax.onnx"
    entropy_model = output_dir / f"{model.stem}.int8.entropy.onnx"

    print(f"[quant] FP32 model: {model}")
    print(f"[quant] calibration images: {len(images)}")
    print(f"[quant] input name: {input_name}")
    # YOLO 导出模型带动态 batch/H/W，ORT symbolic shape 推导可能不完整；
    # 跳过 symbolic 阶段，保留普通 ONNX shape inference，避免预处理误失败。
    quant_pre_process(
        str(model), str(preprocessed_model), skip_symbolic_shape=True
    )

    reader_cls = make_reader_class(calibration_data_reader_base)
    common_kwargs = {
        "model_input": str(preprocessed_model),
        "quant_format": quant_format.QDQ,
        "activation_type": quant_type.QUInt8,
        "weight_type": quant_type.QInt8,
        "per_channel": True,
    }

    quantize_static(
        model_output=str(minmax_model),
        calibration_data_reader=reader_cls(input_name, images, args.input_size, cv2, np),
        calibrate_method=calibration_method.MinMax,
        **common_kwargs,
    )
    print(f"[quant] wrote: {minmax_model}")

    quantize_static(
        model_output=str(entropy_model),
        calibration_data_reader=reader_cls(input_name, images, args.input_size, cv2, np),
        calibrate_method=calibration_method.Entropy,
        **common_kwargs,
    )
    print(f"[quant] wrote: {entropy_model}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
