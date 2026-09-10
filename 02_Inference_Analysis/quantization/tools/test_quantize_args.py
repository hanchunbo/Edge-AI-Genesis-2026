#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# 文件功能：验证校准数量的默认值和边界，不加载推理依赖。

import contextlib
import io
import sys
import unittest
from unittest.mock import patch

from quantize_yolov8_static import parse_args


class QuantArgsTest(unittest.TestCase):
    """校准规模必须与复现文档一致，扩大规模需显式指定。"""

    def test_default_limit_matches_report(self):
        with patch.object(sys, "argv", ["quant", "--model", "unused.onnx"]):
            self.assertEqual(parse_args().calib_limit, 32)

    def test_explicit_limit_is_preserved(self):
        with patch.object(
            sys, "argv", ["quant", "--model", "unused.onnx", "--calib-limit", "8"]
        ):
            self.assertEqual(parse_args().calib_limit, 8)

    def test_explicit_zero_allows_unlimited(self):
        with patch.object(
            sys, "argv", ["quant", "--model", "unused.onnx", "--calib-limit", "0"]
        ):
            self.assertEqual(parse_args().calib_limit, 0)

    def test_negative_limit_is_rejected(self):
        with patch.object(
            sys, "argv", ["quant", "--model", "unused.onnx", "--calib-limit", "-1"]
        ):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    parse_args()
            self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
