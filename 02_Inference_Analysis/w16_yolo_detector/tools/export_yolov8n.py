# SPDX-License-Identifier: MIT
#
# 文件功能：导出 yolov8n.onnx（动态 batch, opset17）+ 落盘 coco 标签与测试图。
#           供 W16 C++ YOLODetector 加载与对拍。
#
# 运行：先激活 venv（见 README），再 `python tools/export_yolov8n.py`。

import shutil
from pathlib import Path

import onnx
from ultralytics import YOLO

# 路径：脚本在 tools/ 下，模型产物统一放 ../models/。
TOOLS_DIR = Path(__file__).resolve().parent
MODELS_DIR = TOOLS_DIR.parent / "models"
MODELS_DIR.mkdir(exist_ok=True)


def main() -> None:
    # 1) 下载预训练权重并导出 ONNX。
    #    dynamic=True 让 batch 维为动态(-1)，对应 W16 的动态 batch 压测；
    #    opset=17 与项目 ORT 1.26 对齐，避免不支持算子。
    model = YOLO("yolov8n.pt")
    exported = model.export(format="onnx", opset=17, dynamic=True, imgsz=640)

    onnx_dst = MODELS_DIR / "yolov8n.onnx"
    shutil.move(str(exported), str(onnx_dst))
    print(f"[export] ONNX -> {onnx_dst}")

    # 2) onnx.checker 验证图结构。
    onnx.checker.check_model(str(onnx_dst))
    m = onnx.load(str(onnx_dst))
    in_shape = [d.dim_param or d.dim_value
                for d in m.graph.input[0].type.tensor_type.shape.dim]
    out_shape = [d.dim_param or d.dim_value
                 for d in m.graph.output[0].type.tensor_type.shape.dim]
    print(f"[export] input={m.graph.input[0].name}:{in_shape} "
          f"output={m.graph.output[0].name}:{out_shape}")

    # 3) 落盘 COCO 80 类标签（每行一个，索引即类别 id）。
    labels_dst = MODELS_DIR / "coco_classes.txt"
    names = model.names  # dict: id -> name
    with labels_dst.open("w") as f:
        for i in range(len(names)):
            f.write(names[i] + "\n")
    print(f"[export] labels({len(names)}) -> {labels_dst}")

    # 4) 拷贝 ultralytics 自带样图作为测试图（含 person/bus，便于肉眼校验）。
    asset = Path(model.ckpt_path).parent if hasattr(model, "ckpt_path") else None
    bus = None
    try:
        from ultralytics.utils import ASSETS
        bus = ASSETS / "bus.jpg"
    except Exception:
        bus = None
    img_dst = MODELS_DIR / "test_image.jpg"
    if bus and bus.exists():
        shutil.copy(str(bus), str(img_dst))
        print(f"[export] test image -> {img_dst}")
    else:
        print("[export] 警告：未找到 ultralytics 样图，请手动放置 test_image.jpg")


if __name__ == "__main__":
    main()
