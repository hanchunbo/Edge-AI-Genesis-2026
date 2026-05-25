# W14 模型文件

本目录用于存放 ONNX 模型文件（不入库，已被 `.gitignore` 排除）。

## 下载方式

### MobileNetV2（首选，~14 MB）

```bash
# 选项 A：ONNX Model Zoo 直接下载
curl -L -o mobilenetv2.onnx \
  https://github.com/onnx/models/raw/main/validated/vision/classification/mobilenet/model/mobilenetv2-12.onnx

# 选项 B：PyTorch 导出（无外网或上面失效时）
python - <<'PY'
import torch, torchvision
m = torchvision.models.mobilenet_v2(weights="DEFAULT").eval()
x = torch.randn(1, 3, 224, 224)
torch.onnx.export(m, x, "mobilenetv2.onnx",
                  input_names=["input"], output_names=["output"],
                  opset_version=17, dynamic_axes=None)
PY
```

### ResNet18（备选，~45 MB）

```bash
# PyTorch 导出
python - <<'PY'
import torch, torchvision
m = torchvision.models.resnet18(weights="DEFAULT").eval()
x = torch.randn(1, 3, 224, 224)
torch.onnx.export(m, x, "resnet18.onnx",
                  input_names=["input"], output_names=["output"],
                  opset_version=17, dynamic_axes=None)
PY
```

## 校验

两个模型导出后，input shape 都应是 `[1, 3, 224, 224] float32`，
output shape `[1, 1000] float32`（ImageNet 1000 类 logits）。

`w14_ort_basics_demo --model <path>` 跑通即视为本目录就绪。
