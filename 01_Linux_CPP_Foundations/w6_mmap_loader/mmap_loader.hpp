// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：基于 mmap 的零拷贝文件加载器（C++20 RAII + Concepts）
// ============================================================================
//
// 【核心思路】
// 传统 fstream::read() 会把文件内容从内核 Page Cache 再拷贝到用户缓冲区，
// 产生 1-2 次用户态内存拷贝，且需要分配与文件等大的堆内存。
//
// mmap 将文件直接映射到进程的虚拟地址空间，内核通过 MMU 页表建立映射，
// 用户态代码直接通过指针读取数据，零次内存拷贝，且仅在首次访问时
// 触发 Page Fault 按需加载（惰性 I/O），大文件启动延迟极低。
//
// 【C++ 现代化要点】
// [Legacy C++11/17]: FILE*/fread + 手动 fclose，错误通过 errno 传递
// [Pain Point]: 内存峰值 = 2x 文件大小；错误处理依赖全局 errno 不线程安全；
//               长度与指针分离传递，调用方易越界；fd 泄漏风险高
// [Modern C++20]: RAII 析构自动 munmap；std::expected 类型安全错误传播；
//                 std::span<const T> 携带长度的类型安全视图；
//                 TriviallyMappable Concept 编译期拒绝非法类型
//
// ============================================================================

#ifndef W6_MMAP_LOADER_MMAP_LOADER_HPP_
#define W6_MMAP_LOADER_MMAP_LOADER_HPP_

#include "mmap_error.hpp"

#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <type_traits>

// POSIX 系统调用头文件（mmap/munmap/madvise/open/close/fstat）
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace w6 {

// ============================================================================
// Concept: TriviallyMappable
// ============================================================================
// 约束 T 必须同时满足三个条件，才能安全地通过 mmap 映射解释为 T 数组：
//
//   1. std::is_trivially_copyable_v<T>
//      - 无用户定义的拷贝/移动/析构函数
//      - 保证 reinterpret_cast<T*>(mmap_ptr) 的内存直接解释是合法的
//
//   2. std::is_standard_layout_v<T>
//      - 成员布局符合 C 语言规则（无虚函数、无混合访问控制）
//      - 保证与磁盘二进制格式一一对应
//
//   3. !std::is_empty_v<T>
//      - sizeof(T) > 0，避免 byte_size / sizeof(T) 除零
//      - 空类型没有实际数据，映射无意义
// ============================================================================
template <typename T>
concept TriviallyMappable = std::is_trivially_copyable_v<T> &&
                            std::is_standard_layout_v<T> && !std::is_empty_v<T>;

// ============================================================================
// MmapLoader<T> —— 零拷贝文件加载器
// ============================================================================
template <TriviallyMappable T>
class MmapLoader {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using span_type = std::span<const T>;

  // ==========================================================================
  // 默认构造：创建空（无效）加载器，Valid() == false
  // ==========================================================================
  MmapLoader() noexcept = default;

  // ==========================================================================
  // 析构：RAII 自动 munmap，无需调用者手动释放
  // ==========================================================================
  ~MmapLoader() { Release(); }

  // ==========================================================================
  // 禁用拷贝：mmap 映射不能共享所有权（否则需引用计数，复杂度倍增）
  // 启用移动：转移所有权，源对象置为空状态
  // ==========================================================================
  MmapLoader(const MmapLoader&) = delete;
  MmapLoader& operator=(const MmapLoader&) = delete;

  MmapLoader(MmapLoader&& other) noexcept
      : addr_(other.addr_), byte_size_(other.byte_size_) {
    // 置空源对象，确保其析构时不会重复 munmap
    other.addr_ = nullptr;
    other.byte_size_ = 0;
  }

  MmapLoader& operator=(MmapLoader&& other) noexcept {
    if (this != &other) {
      Release();  // 先释放当前已持有的映射
      addr_ = other.addr_;
      byte_size_ = other.byte_size_;
      other.addr_ = nullptr;
      other.byte_size_ = 0;
    }
    return *this;
  }

  // ==========================================================================
  // 静态工厂函数 Load() —— 唯一构造有效 MmapLoader 的入口
  // ==========================================================================
  // 使用 std::expected 而非异常原因：
  //   - 边缘设备通常以 -fno-exceptions 编译
  //   - 文件不存在等 I/O 错误属于预期失败，不是编程错误
  //   - [[nodiscard]] 强制调用方处理错误，不可忽略
  // ==========================================================================
  [[nodiscard]] static auto Load(const std::filesystem::path& path)
      -> std::expected<MmapLoader, MmapError> {
    // 步骤 1：以只读方式打开文件，拿到文件描述符（fd）
    // O_RDONLY 不要求目标文件有写权限，适合只读模型权重场景。
    // fd 是内核给进程的「凭证」（一个小整数，如 3/4/5），后续所有操作凭它进行。
    // :: 前缀（全局作用域限定符）：明确调用全局命名空间里的 POSIX open()，
    //   而非当前类或命名空间中可能同名的成员函数，等同于文件路径中的「绝对路径」。
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
      return std::unexpected(MmapError::kOpenFailed);
    }

    // 步骤 2：获取文件元信息（主要目的：得到 st_size）
    // fstat 接受已打开的 fd，而非文件路径：
    //   - 比 stat(path) 少一次路径解析开销；
    //   - 避免 TOCTOU 竞态（文件打开后被另一进程替换的安全漏洞）。
    // st.st_size 是 mmap 必须知道的映射字节数。
    struct stat st{};
    if (::fstat(fd, &st) == -1) {
      ::close(fd);
      return std::unexpected(MmapError::kStatFailed);
    }

    // 步骤 3：空文件检查
    // mmap(nullptr, 0, ...) 的行为是未定义的，必须提前拒绝
    auto byte_size = static_cast<size_type>(st.st_size);
    if (byte_size == 0) {
      ::close(fd);
      return std::unexpected(MmapError::kEmptyFile);
    }

    // 步骤 4：文件大小对齐检查
    // 若 byte_size % sizeof(T) != 0，Span() 末尾会有不完整的元素，
    // 强制解释为 T 会导致未定义行为，提前报错更安全
    if (byte_size % sizeof(T) != 0) {
      ::close(fd);
      return std::unexpected(MmapError::kSizeMismatch);
    }

    // 步骤 5：建立内存映射（核心系统调用）
    // mmap 将文件直接映射到进程虚拟地址空间，用户态代码通过指针零拷贝读取数据；
    // 传统 read() 需额外将内核 Page Cache 拷贝到用户缓冲区，mmap
    // 省去了这次拷贝。
    //   nullptr    → 让内核自动选择映射地址
    //   byte_size  → 映射的字节范围
    //   PROT_READ  → 只读保护，任何写操作触发 SIGSEGV
    //   MAP_PRIVATE→ 私有映射（写时复制），修改不回写磁盘，比 MAP_SHARED 更安全
    //   fd, 0      → 从文件头（offset=0，满足页对齐要求）开始映射
    // 调用成功后 addr 即为文件内容在内存中的起始地址，按需 Page Fault
    // 惰性加载。
    void* addr = ::mmap(nullptr, byte_size, PROT_READ, MAP_PRIVATE, fd, 0);

    // 步骤 6：mmap 建立后立即关闭 fd
    // POSIX 保证：mmap 成功后映射不依赖 fd 持续存在，可立即关闭。
    // 及时关闭是防御性习惯——边缘设备每进程 fd 上限通常为 1024，
    // 长期持有多余 fd 会导致「Too many open files」错误。
    ::close(fd);

    if (addr == MAP_FAILED) {
      return std::unexpected(MmapError::kMmapFailed);
    }

    // 步骤 7：以私有构造创建有效对象
    return MmapLoader(addr, byte_size);
  }

  // ==========================================================================
  // 数据视图接口
  // ==========================================================================

  // 返回 std::span<const T>：携带长度信息的零开销视图
  // 调用方可直接用于 std::ranges 算法、范围 for、subspan 切片
  [[nodiscard]] span_type Span() const noexcept {
    if (addr_ == nullptr) return {};
    // addr_ 是 void*（mmap
    // 返回无类型指针，内核只管地址，不知道存的是什么类型）。 std::span 不接受
    // void*，需分两步转换：
    //   1. static_cast<const T*>(addr_)：程序员对编译器的承诺——
    //      「这块内存里存的是 T 类型数据，请按 T 解释它」。
    //   2. byte_size_ / sizeof(T)：将字节数换算为元素个数
    //      （例：128 MB / 4 = 33,554,432 个 float）。
    // std::span 拿到「首元素指针 +
    // 元素个数」后，即可安全遍历、切片，无任何数据拷贝。
    return span_type(static_cast<const T*>(addr_), byte_size_ / sizeof(T));
  }

  // 隐式转换：允许将 MmapLoader 直接传给接受 std::span<const T> 的函数
  operator span_type() const noexcept { return Span(); }

  // ==========================================================================
  // 容量查询
  // ==========================================================================

  // 元素数量（byte_size / sizeof(T)，由 Load() 已验证可整除）
  [[nodiscard]] size_type Size() const noexcept {
    return byte_size_ / sizeof(T);
  }

  // 映射区域的字节数（用于调试和断言）
  [[nodiscard]] size_type SizeBytes() const noexcept { return byte_size_; }

  // 是否持有有效映射（默认构造或移动后为 false）
  [[nodiscard]] bool Valid() const noexcept { return addr_ != nullptr; }

  [[nodiscard]] explicit operator bool() const noexcept { return Valid(); }

  // ==========================================================================
  // madvise 性能提示
  // ==========================================================================
  // madvise 通过通知内核数据访问模式来优化 Page Cache 预取策略。
  // 以下函数均为 best-effort，失败时静默忽略（不影响正确性）。

  // MADV_SEQUENTIAL：预期按顺序扫描全文件（如模型加载时逐层读取权重）
  // 内核会激进预读后续页面，降低 Page Fault 频率
  void AdviseSequential() const noexcept {
    if (addr_) ::madvise(addr_, byte_size_, MADV_SEQUENTIAL);
  }

  // MADV_WILLNEED：尽快异步预取数据到物理内存（相当于后台 read-ahead）
  // 适合已知后续会立即密集访问的场景（如推理前预热）
  void Prefetch() const noexcept {
    if (addr_) ::madvise(addr_, byte_size_, MADV_WILLNEED);
  }

  // MADV_RANDOM：预期随机访问（如稀疏权重、注意力 KV Cache）
  // 内核禁用顺序预读，节省 I/O 带宽
  void AdviseRandom() const noexcept {
    if (addr_) ::madvise(addr_, byte_size_, MADV_RANDOM);
  }

 private:
  // 私有构造：仅由 Load() 工厂函数调用，确保外部无法绕过验证逻辑
  MmapLoader(void* addr, size_type byte_size) noexcept
      : addr_(addr), byte_size_(byte_size) {}

  // 释放映射：munmap 时必须传入与 mmap 完全相同的 addr 和 length
  void Release() noexcept {
    if (addr_ != nullptr) {
      ::munmap(addr_, byte_size_);
      addr_ = nullptr;
      byte_size_ = 0;
    }
  }

  void* addr_ = nullptr;     // mmap 返回的虚拟地址（nullptr 表示无效状态）
  size_type byte_size_ = 0;  // 映射的字节数，munmap 时必须与 mmap 一致
};

}  // namespace w6

#endif  // W6_MMAP_LOADER_MMAP_LOADER_HPP_
