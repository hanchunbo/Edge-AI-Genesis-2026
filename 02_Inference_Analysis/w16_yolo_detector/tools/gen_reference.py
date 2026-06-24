# SPDX-License-Identifier: MIT
#
# 文件功能：用 ultralytics 跑导出的 ONNX，生成 C++ 对拍基准 reference_detections.txt。
#
# 关键对齐项（任一不一致都会让对拍失败）：
#   - conf/iou 与 C++ DetectorConfig 默认一致（0.25 / 0.45）；
#   - 直接对**导出的 onnx**推理（而非 .pt），消除 pt↔onnx 差异；
#   - rect=False 强制**方形 640×640 letterbox**：本 onnx 导出为动态 H/W，
#     ultralytics 默认会走矩形推理（如 640×480 无填充），而 C++ 端固定喂
#     方形 640×640（带灰边）。两者对置信度高的目标差异可忽略，但对临界框
#     （score≈0.25）会改变检测数量。强制方形后两端逐框 score 对齐到 <0.005。
#
# 输出每行："class_id score x1 y1 x2 y2"（原图坐标系，xyxy）。

from pathlib import Path

from ultralytics import YOLO

TOOLS_DIR = Path(__file__).resolve().parent
MODELS_DIR = TOOLS_DIR.parent / "models"

CONF = 0.25  # 对齐 w16::DetectorConfig::conf_thresh
IOU = 0.45   # 对齐 w16::DetectorConfig::iou_thresh


def main() -> None:
    onnx_path = MODELS_DIR / "yolov8n.onnx"
    img_path = MODELS_DIR / "test_image.jpg"
    out_path = MODELS_DIR / "reference_detections.txt"
    for p in (onnx_path, img_path):
        if not p.exists():
            raise SystemExit(f"缺少 {p}，请先运行 export_yolov8n.py")

    model = YOLO(str(onnx_path))
    results = model.predict(str(img_path), conf=CONF, iou=IOU, imgsz=640,
                            rect=False, verbose=False)
    r = results[0]

    lines = []
    for box in r.boxes:
        cls = int(box.cls.item())
        score = float(box.conf.item())
        x1, y1, x2, y2 = (float(v) for v in box.xyxy[0].tolist())
        lines.append(f"{cls} {score:.4f} {x1:.2f} {y1:.2f} {x2:.2f} {y2:.2f}")

    out_path.write_text("\n".join(lines) + "\n")
    print(f"[reference] {len(lines)} 个检测框 (conf={CONF}, iou={IOU}) -> "
          f"{out_path}")
    for ln in lines:
        print("  " + ln)


if __name__ == "__main__":
    main()
