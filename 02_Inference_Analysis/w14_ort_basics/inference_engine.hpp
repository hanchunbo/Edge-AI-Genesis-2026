// SPDX-License-Identifier: MIT
//
// 文件功能：W14 InferenceEngine 类声明 —— RAII 封装 ONNX Runtime C++ Session，
//           支持模型加载、I/O 元数据查询、零拷贝输入推理。

#ifndef EDGE_AI_GENESIS_2026_W14_INFERENCE_ENGINE_HPP_
#define EDGE_AI_GENESIS_2026_W14_INFERENCE_ENGINE_HPP_

#include <memory>
#include <onnxruntime_cxx_api.h>
#include <span>
#include <string>
#include <vector>

namespace w14 {

// ExecutionProvider 选择。kCuda 不可用时构造函数优雅回退 kCpu（见 .cpp）。
enum class Ep { kCpu, kCuda };

// 会话配置（W16 引入，用于线程调优）。线程数为 0 表示用 ORT 默认。
//   - intra_op_threads：单算子内部并行度（如一个大 MatMul 切多线程算）。
//   -
//   inter_op_threads：算子间并行度（仅对有并行分支的图有意义；串行图无收益）。
struct SessionConfig {
  Ep ep = Ep::kCpu;
  int intra_op_threads = 0;
  int inter_op_threads = 0;
  // 可选借用运行时；调用方必须让 Env 比所有相关会话及输出活得更久。
  // nullptr 保持旧 GlobalEnv 路径；quant 用入口局部 Env 控制退出顺序。
  Ort::Env* env = nullptr;
};

// InferenceEngine 封装一个 ONNX 模型推理会话。
//
// 设计要点：
//   - RAII：构造时加载模型并查询 I/O 元数据；析构由 unique_ptr 自动释放
//   Session。
//   - 错误模型：任何加载/查询失败抛 std::runtime_error，带上下文。
//   - 零拷贝：Run() 接受 std::span，直接借用调用方 buffer 创建 Ort::Value，
//             不做 memcpy；调用方负责保证 buffer 在 Run 返回前存活。
//   - 共享 Env：调用方可统一提供运行时；未指定时走旧 GlobalEnv()。
class InferenceEngine {
 public:
  // I/O 节点的静态元数据，构造时一次性查出来缓存。
  struct IoInfo {
    std::string name;
    std::vector<int64_t> shape;  // 含 -1 表示动态维
    ONNXTensorElementDataType dtype;
  };

  // ep 默认 kCpu，保持既有调用方（W15 Classifier / demo / 旧测试）零改动。
  // 请求 kCuda 但环境不可用时不抛异常，回退 CPU；用 ActiveEp() 查实际生效的
  // EP。内部委托到 SessionConfig 版本（线程数取默认）。
  explicit InferenceEngine(const std::string& model_path, Ep ep = Ep::kCpu);
  // W16 新增：带线程调优的构造。加性扩展，既有调用方不受影响。
  InferenceEngine(const std::string& model_path, const SessionConfig& cfg);
  ~InferenceEngine();

  InferenceEngine(const InferenceEngine&) = delete;
  InferenceEngine& operator=(const InferenceEngine&) = delete;
  InferenceEngine(InferenceEngine&&) noexcept;
  InferenceEngine& operator=(InferenceEngine&&) noexcept;

  [[nodiscard]] const std::vector<IoInfo>& Inputs() const { return inputs_; }
  [[nodiscard]] const std::vector<IoInfo>& Outputs() const { return outputs_; }

  // 实际生效的 EP。注意：仅表示 Session 成功请求/注册了该 EP，
  // 不证明每个算子都落到该设备——kernel placement 严格确认留 W16/W18 profiling。
  [[nodiscard]] Ep ActiveEp() const { return active_ep_; }
  // CUDA 回退到 CPU 的原因（Ort::Exception 文本）；未回退时为空串。
  [[nodiscard]] const std::string& EpFallbackReason() const {
    return ep_fallback_reason_;
  }

  // 单输入模型用：零拷贝构造输入 Tensor，跑一次推理，返回所有输出 Value。
  // 多输入模型在 W15 接前后处理时再扩展。
  [[nodiscard]] std::vector<Ort::Value> Run(std::span<const float> input,
                                            std::span<const int64_t> shape);

  // W16 新增：用 Ort::IoBinding 跑一次推理。与 Run 的差别是复用一个持久
  // binding：省去每次 Run 重新搭 I/O 名称数组与输出 Ort::Value 的分配，
  // CUDA EP 下让 ORT 复用设备/主机缓冲，减少 Device→Host 拷贝抖动。
  // 语义同 Run：零拷贝借用 input，返回输出 Value（已拷回 CPU，可直接读）。
  // 契约：持久输出绑定假定**同一 engine 实例的输出形状固定**（即 batch /
  // 输入尺寸不变）；要换 batch 请用新的 engine 实例。
  [[nodiscard]] std::vector<Ort::Value> RunIoBinding(
      std::span<const float> input, std::span<const int64_t> shape);

  // 旧进程级 Env：函数内 static 保证唯一 + 线程安全初始化。
  // CUDA 动态库的静态析构顺序可能使其释放过晚；新入口宜借用局部 Env。
  // 此接口为历史调用方保留；测试代码也用它的地址做"单例"断言。
  // 共享运行环境并不要求 C++ 包装对象为 static，生命周期概念见主题库。
  static Ort::Env& GlobalEnv();

 private:
  std::unique_ptr<Ort::Session> session_;
  std::vector<IoInfo> inputs_;
  std::vector<IoInfo> outputs_;
  Ep active_ep_ = Ep::kCpu;
  std::string ep_fallback_reason_;
  // 持久 IoBinding：首次 RunIoBinding 时惰性创建并复用（见 .cpp）。
  std::unique_ptr<Ort::IoBinding> io_binding_;
};

}  // namespace w14

#endif  // EDGE_AI_GENESIS_2026_W14_INFERENCE_ENGINE_HPP_
