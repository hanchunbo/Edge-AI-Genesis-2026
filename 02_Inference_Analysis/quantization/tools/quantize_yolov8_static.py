#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# 文件功能：用 ONNX Runtime static PTQ 为 YOLOv8n 生成 MinMax / Entropy INT8 QDQ 模型。

"""ORT static PTQ 生成 YOLOv8n 的 INT8 QDQ 模型。

默认排除检测头 `/model.22/`（154 节点保 FP32）：整图量化时 YOLOv8 输出张量
(1,84,8400) 混合框坐标(0~640)与类别分数(0~1)，Concat 输出的 per-tensor
scale≈2.5 会把所有分数压成 0，检测框全丢——与校准集大小无关。
根因与修复见 ../notes.md §INT8 0 检测框根因与修复。

内存约束：Entropy 校准在内存中累积全部中间层输出做直方图，32 图峰值 RSS 约
5.24GB。本机 WSL 仅 7.7GB，必须包内存墙跑（裸跑曾多次打崩 WSL VM）：

    systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=4G <cmd>

被杀时降 --calib-limit，不要提高内存上限。
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable


IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""
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
    parser.add_argument(
        "--calib-limit",
        type=int,
        default=0,
        help=(
            "校准图数量上限（0 表示不限）。Entropy 校准会在内存中累积全部中间层"
            "输出做直方图，低内存环境需限制图数避免 OOM。"
        ),
    )
    parser.add_argument(
        "--exclude-pattern",
        default="/model.22/",
        help=(
            "节点名包含该子串的节点不量化（默认排除 YOLOv8 检测头 /model.22/）。"
            "传空字符串表示整图量化。"
        ),
    )
    return parser.parse_args()


def import_runtime_deps():
    """惰性导入量化依赖，缺失时给出可执行的安装提示。

    Returns:
        dict: 依赖名 → 模块/类的映射。

    Note:
        写成惰性导入是为了让 --help 在没装 onnxruntime 的环境也能跑通。
    """
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
    """汇总校准图片路径（目录内按文件名排序）。

    Args:
        calib_images: --calib-image 传入的单图路径。
        calib_dirs: --calib-dir 传入的目录，递归收集受支持的图片后缀。

    Returns:
        校准图路径列表。

    Raises:
        SystemExit: 路径不存在，或最终一张图都没收到。
    """
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
    """等比缩放 + 灰边(114)填充到 size×size。

    Note:
        必须与 C++ 侧 w10::LetterboxToTensor 保持同一套变换——校准数据的分布
        要和推理时一致，否则激活范围偏移，量化 scale 就选错了。
    """
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
    """读图并预处理成 NCHW float32 张量（BGR→RGB + letterbox + /255）。"""
    bgr = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if bgr is None:
        raise RuntimeError(f"图片读取失败：{path}")
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    letterboxed = letterbox_rgb(rgb, size, cv2, np)
    tensor = letterboxed.astype(np.float32) / 255.0
    tensor = np.transpose(tensor, (2, 0, 1))
    return np.expand_dims(tensor, axis=0)


def make_reader_class(calibration_data_reader_base):
    """构造 CalibrationDataReader 子类（懒加载逐张预处理）。

    Note:
        懒加载不是风格选择：一次性预处理全部校准图会让 RSS 再涨一截，
        叠加 Entropy 的直方图累积会直接打爆 WSL。见模块 docstring 的内存约束。
    """
    # 懒加载：逐张预处理而非全量驻留内存。128 张 640×640 float32 输入约 630MB，
    # 在低内存环境（WSL 7.7GiB）会挤占 Entropy 校准本就吃紧的直方图内存。
    class YoloCalibrationDataReader(calibration_data_reader_base):
        def __init__(self, input_name: str, image_paths: list[Path], size: int, cv2, np):
            self.input_name = input_name
            self.image_paths = image_paths
            self.size = size
            self.cv2 = cv2
            self.np = np
            self.index = 0

        def get_next(self):
            if self.index >= len(self.image_paths):
                return None
            path = self.image_paths[self.index]
            self.index += 1
            return {
                self.input_name: preprocess_image(path, self.size, self.cv2, self.np)
            }

        def rewind(self):
            self.index = 0

    return YoloCalibrationDataReader


def main() -> int:
    """生成 MinMax 与 Entropy 两个 INT8 模型。

    Returns:
        进程退出码，0 表示成功。

    Warning:
        ORT `quantize_static` 默认参数下 Entropy 实测退化为 MinMax——本模块两个
        INT8 产物字节级相同（2026-07-11 勘误）。产出两个文件是为了保留对比接口，
        不代表两种校准策略真的生效。见 ../notes.md §概念边界速查。
    """
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
    if args.calib_limit > 0:
        images = images[: args.calib_limit]

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

    # 检测头必须留 FP32：YOLOv8 输出张量混合框坐标（0~640）与类别分数（0~1），
    # per-tensor 激活量化的 scale≈640/255≈2.5，会把所有 <2.5 的分数坍缩成 0，
    # 导致 INT8 模型 0 检测框（与校准集大小无关）。头部算力占比小，保 FP32
    # 对延迟影响可忽略。
    nodes_to_exclude: list[str] = []
    if args.exclude_pattern:
        import onnx  # noqa: PLC0415 — 与其余重依赖一致，延迟到运行期导入

        graph = onnx.load(str(preprocessed_model)).graph
        nodes_to_exclude = [
            n.name for n in graph.node if args.exclude_pattern in n.name
        ]
        print(
            f"[quant] exclude nodes: {len(nodes_to_exclude)} "
            f"(pattern: {args.exclude_pattern!r})"
        )

    reader_cls = make_reader_class(calibration_data_reader_base)
    common_kwargs = {
        "model_input": str(preprocessed_model),
        "quant_format": quant_format.QDQ,
        "activation_type": quant_type.QUInt8,
        "weight_type": quant_type.QInt8,
        "per_channel": True,
        "nodes_to_exclude": nodes_to_exclude,
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
