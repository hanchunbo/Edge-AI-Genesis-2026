# W6：mmap + std::span 零拷贝文件加载器

**目标**：用 `mmap` 替代 `fstream` 加载模型权重文件，用 `std::span` 提供类型安全视图。

---

## 核心机制

### 传统 I/O vs mmap

```
传统 read():  磁盘 →[DMA]→ Page Cache →[copy]→ 用户缓冲区   ← 2 次拷贝
mmap:         磁盘 →[DMA]→ Page Cache →[MMU页表]→ 虚拟地址   ← 0 次用户态拷贝
```

mmap 调用后**立即返回**，物理内存 = 0；首次访问某地址时触发 Page Fault，内核才加载对应 4KB。

### 关键 syscall 参数

```cpp
// open
int fd = open(path, O_RDONLY);  // 只读映射不需要写权限

// fstat 获取文件大小
struct stat st;
fstat(fd, &st);
size_t file_size = st.st_size;

// mmap
void* addr = mmap(
    nullptr,           // 让内核选地址
    file_size,         // 映射长度（字节）
    PROT_READ,         // 只读
    MAP_PRIVATE,       // 私有映射，COW 隔离
    fd,                // 文件描述符
    0                  // 偏移量，必须页对齐
);
// 返回 MAP_FAILED (== (void*)-1) 表示失败

// 释放
munmap(addr, file_size);
close(fd);

// 性能提示
madvise(addr, file_size, MADV_SEQUENTIAL);  // 顺序读
madvise(addr, file_size, MADV_WILLNEED);    // 预取
```

### std::span 要点

```cpp
// span 是非拥有视图，不分配内存
std::span<const float> s(ptr, count);

s.data()           // 原始指针
s.size()           // 元素数
s.size_bytes()     // 字节数
s.subspan(off, n)  // 切片，仍是 O(1)
s.first(n)
s.last(n)

// 从 void* 转为 float*（mmap 结果）
auto* typed = static_cast<const float*>(addr);
std::span<const float> weights(typed, file_size / sizeof(float));
```

---

## 设计方案

### 文件结构

```
w6_mmap_loader/
├── include/
│   ├── mmap_loader.hpp       # RAII 加载器（主类）
│   └── mmap_error.hpp        # 错误枚举 + std::expected
├── src/
│   └── mmap_loader.cpp       # 实现
├── tests/
│   └── mmap_loader_test.cpp
└── CMakeLists.txt
```

### C++20 Concept 约束

```cpp
// 约束 T 必须可安全 mmap 映射（平凡可复制 + 标准布局 + 非空）
template <typename T>
concept TriviallyMappable =
    std::is_trivially_copyable_v<T> &&
    std::is_standard_layout_v<T> &&
    !std::is_empty_v<T>;
```

### MmapLoader 接口（最小设计）

```cpp
template <TriviallyMappable T>
class MmapLoader {
 public:
    MmapLoader() noexcept = default;
    ~MmapLoader();                               // munmap + close

    MmapLoader(const MmapLoader&) = delete;
    MmapLoader& operator=(const MmapLoader&) = delete;
    MmapLoader(MmapLoader&&) noexcept;
    MmapLoader& operator=(MmapLoader&&) noexcept;

    // 工厂函数，避免异常，用 std::expected 报错
    [[nodiscard]] static auto Load(const std::filesystem::path& path)
        -> std::expected<MmapLoader, MmapError>;

    [[nodiscard]] std::span<const T> Span() const noexcept;
    [[nodiscard]] size_t Size() const noexcept;   // 元素数
    [[nodiscard]] bool Valid() const noexcept;

    void AdviseSequential() const noexcept;
    void Prefetch() const noexcept;

 private:
    void*  addr_      = nullptr;
    size_t byte_size_ = 0;
    int    fd_        = -1;
};
```

### 错误处理

```cpp
enum class MmapError {
    kOpenFailed,
    kStatFailed,
    kEmptyFile,
    kSizeMismatch,   // 文件大小不是 sizeof(T) 的整数倍
    kMmapFailed,
};
```

---

## 实现要点 & 常见坑

| 问题 | 说明 |
|------|------|
| `MAP_PRIVATE` vs `MAP_SHARED` | 只读加载用 `MAP_PRIVATE`，修改不回写磁盘 |
| 偏移量页对齐 | `LoadPartial` 的 offset 必须是 `getpagesize()` 的整数倍 |
| 文件大小 = 0 | `mmap(size=0)` 是 UB，需提前判断 |
| 文件大小 % sizeof(T) | 不整除时 span 元素数截断，应报错 |
| 移动后置空 | 移动构造后原对象 `addr_=-1, fd_=-1`，析构时跳过 munmap/close |
| SIGBUS | 访问超出文件大小的已映射区域会触发 SIGBUS，而非 SIGSEGV |

---

## 实现 Checklist

- [x] `MmapError` 枚举 + `std::expected` 返回
- [x] `MmapLoader<T>::Load()` 工厂函数（open/fstat/mmap）
- [x] 析构 / 移动语义
- [x] `Span()` 返回 `std::span<const T>`
- [x] `madvise` 包装（AdviseSequential / Prefetch）
- [x] 测试：正常加载 float 数组
- [x] 测试：文件大小不对齐返回 `kSizeMismatch`
- [x] 测试：不存在的文件返回 `kOpenFailed`
- [ ] Benchmark：mmap vs `std::ifstream` 读 128MB 文件

---

## 编译 & 验证

```bash
# 配置（首次或清理后）
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-13

# 只编译 w6
cmake --build build --target w6_mmap_loader_test -j$(nproc)

# 只跑 w6 测试
ctest --test-dir build -R W6_MmapLoaderTest --output-on-failure

# 格式检查（CI 必须通过）
find . -maxdepth 3 -regex '.*0[1-4]_.*' \( -name "*.cpp" -o -name "*.hpp" \) \
  | xargs clang-format --dry-run --Werror
```

加强验证：

```bash
# AddressSanitizer（越界 / use-after-free）
cmake -B build -DCMAKE_CXX_COMPILER=g++-13 -DENABLE_ASAN=ON
cmake --build build --target w6_mmap_loader_test -j$(nproc)
ctest --test-dir build -R W6_MmapLoaderTest --output-on-failure

# Valgrind（fd 泄漏 / mmap 未释放）
valgrind --track-fds=yes --leak-check=full \
  ./build/01_Linux_CPP_Foundations/w6_mmap_loader/w6_mmap_loader_test
```
