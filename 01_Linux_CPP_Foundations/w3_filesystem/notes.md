# W3 知识笔记：C++17 跨平台实战特性与视图优化

## 目录

1. [std::filesystem](#stdfilesystem)
2. [std::optional](#stdoptional)
3. [std::string_view](#stdstring_view)
4. [结构化绑定](#结构化绑定)
5. [性能优化总结](#性能优化总结)

---

## std::filesystem

### 简介

C++17 引入的跨平台文件系统库，统一了 Linux/Windows/macOS 的路径处理逻辑。

### 核心类型

| 类型 | 说明 |
|------|------|
| `fs::path` | 表示文件系统路径，支持 `/` 运算符拼接 |
| `fs::directory_entry` | 表示目录中的一个条目 |
| `fs::directory_iterator` | 非递归遍历目录 |
| `fs::recursive_directory_iterator` | **递归**遍历目录（常用！） |

### 常用操作

```cpp
namespace fs = std::filesystem;

// 检查路径
fs::exists(path);           // 是否存在
fs::is_regular_file(path);  // 是否为普通文件
fs::is_directory(path);     // 是否为目录

// 获取信息
fs::file_size(path);        // 文件大小（字节）
path.extension();           // 扩展名（如 ".onnx"）
path.filename();            // 文件名（如 "model.onnx"）
path.stem();                // 无扩展名文件名（如 "model"）
path.parent_path();         // 父目录

// 目录操作
fs::create_directories(path);  // 递归创建目录
fs::remove_all(path);          // 递归删除目录

// 路径拼接
fs::path full = base / "subdir" / "file.txt";
```

### AI 部署场景应用

- 递归扫描 `/models` 目录查找 `.onnx`/`.engine` 文件
- 创建日志输出目录
- 管理模型缓存文件

---

## std::optional

### 简介

一个**可能包含值也可能为空**的容器，用于替代：
- 返回指针 + nullptr 表示失败
- 返回 bool + 引用参数输出
- 抛出异常表示非异常情况的失败

### 核心操作

```cpp
// 创建
std::optional<int> value = 42;      // 有值
std::optional<int> empty;           // 空
std::optional<int> null = std::nullopt;  // 显式空

// 检查
if (value.has_value()) { ... }  // 方式 1
if (value) { ... }              // 方式 2（推荐）

// 获取值
value.value();              // 若空则抛 bad_optional_access
value.value_or(0);          // 若空则返回默认值
*value;                     // 解引用（需确保有值）

// C++17 if-init 语法
if (auto v = GetOptionalValue(); v) {
  std::cout << *v << std::endl;
}
```

### 典型模式

```cpp
// 函数返回 optional 表示可能失败
std::optional<ModelConfig> LoadConfig(std::string_view path) {
  if (!fs::exists(path)) {
    return std::nullopt;  // 优雅的"失败"返回
  }
  return ParseConfig(path);
}

// 调用方处理
if (auto config = LoadConfig("config.yaml"); config) {
  UseConfig(*config);
} else {
  UseDefaultConfig();
}
```

### AI 部署场景应用

- 加载模型文件（可能不存在）
- 解析配置文件中的可选字段
- 查找 GPU 设备（可能不可用）

---

## std::string_view

### 简介

字符串的**"视图"**，不拥有数据、不分配内存。只是指向现有字符串数据的指针 + 长度。

### 性能优势（关键！）

| 操作 | std::string | std::string_view |
|------|-------------|------------------|
| 传递参数 | 可能拷贝 | 零拷贝 |
| 从 const char* 创建 | O(n) 分配 | O(1) |
| 子串操作 | 新分配内存 | 共享底层数据 |

### 使用示例

```cpp
// 函数参数使用 string_view
void ProcessPath(std::string_view path) {
  // 无论传入 const char* 还是 std::string，都不会拷贝
  std::cout << path << std::endl;
}

ProcessPath("literal string");     // 无拷贝
ProcessPath(std::string("hello")); // 无拷贝

// 常量使用 constexpr string_view
constexpr std::string_view kExtension = ".onnx";  // 编译期确定
```

### 注意事项

```cpp
// ❌ 危险：返回指向局部变量的 string_view
std::string_view BadFunction() {
  std::string local = "hello";
  return local;  // 悬空引用！
}

// ✅ 正确：确保底层数据生命周期 > string_view
std::string_view GoodFunction(const std::string& s) {
  return s.substr(0, 5);  // ❌ 这也是危险的，substr 返回新 string
}

// ✅ 正确做法
std::string_view GoodFunction(std::string_view s) {
  return s.substr(0, 5);  // ✅ string_view::substr 返回 string_view
}
```

### AI 部署场景应用

- 解析日志/配置文件中的字段
- 处理模型元数据中的字符串
- 高频路径字符串操作（避免频繁 new/delete）

---

## 结构化绑定

### 简介

C++17 允许将**元组、pair、数组或具有公共成员的结构体**的多个元素绑定到独立的变量中。

### 语法

```cpp
auto [var1, var2, ...] = expression;
```

### 典型用例

```cpp
// 1. 遍历 map 时同时获取 key 和 value
std::map<std::string, int> scores = {{"Alice", 95}, {"Bob", 87}};
for (const auto& [name, score] : scores) {
  std::cout << name << ": " << score << std::endl;
}

// 2. 函数返回多个值
std::tuple<bool, std::string> ParseFile(std::string_view path) {
  // ...
  return {true, "Success"};
}
auto [success, message] = ParseFile("config.yaml");

// 3. 解构自定义结构体
struct Point { int x, y; };
Point p{10, 20};
auto [x, y] = p;

// 4. 数组解构
int arr[] = {1, 2, 3};
auto [a, b, c] = arr;
```

### AI 部署场景应用

- 遍历模型文件列表时解构元数据
- 处理配置项的 key-value 对
- 解析模型输入/输出张量信息

---

## 性能优化总结

### 本项目优化点

| 优化技术 | 实现方式 | 效果 |
|----------|----------|------|
| 常量扩展名使用 `constexpr string_view` | 编译期确定 | 零运行时开销 |
| 函数参数使用 `string_view` | 传入时无需创建临时 string | 减少内存分配 |
| 结果使用 `const auto&` 接收 | 避免拷贝整个 vector | 减少内存拷贝 |
| 使用 `std::move` 入栈 | 转移所有权而非拷贝 | 减少深拷贝 |

### 代码审查要点

- [ ] 函数参数：只读字符串是否用 `string_view`？
- [ ] 常量字符串：是否用 `constexpr string_view`？
- [ ] 返回值：是否用 `optional` 替代 nullptr/bool 模式？
- [ ] 遍历 map/结构体：是否用结构化绑定？
- [ ] string_view 生命周期：底层数据是否有效？

---

## 编译注意事项

```bash
# 需要 C++17 标准
g++ -std=c++17 model_scanner.cpp -o model_scanner

# GCC 8 及以下需要链接 stdc++fs
g++ -std=c++17 model_scanner.cpp -o model_scanner -lstdc++fs
```
