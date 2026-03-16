#!/bin/bash
# Copyright 2026 Edge-AI-Genesis
#
# ============================================================================
# 文件功能：离线安装 OpenCV 4.10.0 core + imgproc 及其全部传递依赖
# 用法：sudo bash third_party/opencv-debs/install.sh
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> 安装 OpenCV 4.10.0 离线包（core + imgproc）..."
sudo dpkg -i "${SCRIPT_DIR}"/*.deb || true

echo "==> 修复可能存在的依赖问题..."
sudo apt-get install -f -y

echo "==> 验证安装..."
if [ -f /usr/include/opencv4/opencv2/core.hpp ]; then
    echo "✓ OpenCV 头文件安装成功: /usr/include/opencv4/"
else
    echo "✗ 安装失败，请检查日志"
    exit 1
fi
