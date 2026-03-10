// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：mmap 加载器错误类型定义
// ============================================================================

#ifndef W6_MMAP_LOADER_MMAP_ERROR_HPP_
#define W6_MMAP_LOADER_MMAP_ERROR_HPP_

#include <string_view>

namespace w6 {

// 枚举每种 mmap 加载失败场景，配合 std::expected 无异常传播错误
enum class MmapError {
  kOpenFailed,    // open() 失败：路径不存在或无读权限
  kStatFailed,    // fstat() 失败：无法获取文件元信息
  kEmptyFile,     // 文件大小为 0：mmap(size=0) 是未定义行为
  kSizeMismatch,  // 文件字节数不是 sizeof(T) 的整数倍，末尾有悬空字节
  kMmapFailed,    // mmap() 系统调用失败：通常是地址空间不足
};

[[nodiscard]] constexpr std::string_view MmapErrorToString(
    MmapError err) noexcept {
  switch (err) {
    case MmapError::kOpenFailed:
      return "open() failed: file not found or no read permission";
    case MmapError::kStatFailed:
      return "fstat() failed: cannot get file metadata";
    case MmapError::kEmptyFile:
      return "file is empty: mmap(size=0) is undefined behavior";
    case MmapError::kSizeMismatch:
      return "file size is not a multiple of sizeof(T)";
    case MmapError::kMmapFailed:
      return "mmap() failed: address space exhausted or permission denied";
  }
  return "unknown error";
}

}  // namespace w6

#endif  // W6_MMAP_LOADER_MMAP_ERROR_HPP_
