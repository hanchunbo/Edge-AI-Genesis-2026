// Copyright 2026 Edge-AI-Genesis
// 文件功能：通用线程池架构 - 支持泛型任务提交和异步结果获取
//
// 知识点：
// 1. 基于 std::queue 的任务队列设计
// 2. std::future 与 std::packaged_task 异步结果获取
// 3. 泛型任务提交（模板编程 + 可变参数）
// 4. 线程池优雅关闭策略

#ifndef W5_THREAD_POOL_THREAD_POOL_HPP_
#define W5_THREAD_POOL_THREAD_POOL_HPP_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace w5 {

// =============================================================================
// ThreadPool 类 - 通用线程池
// =============================================================================
// 设计目标：
// 1. 支持提交任意可调用对象（函数、Lambda、std::function）
// 2. 返回 std::future 以获取异步执行结果
// 3. 线程在无任务时低功耗挂起（CPU 占用趋近 0%）
// 4. 支持优雅关闭（等待所有任务完成）
// =============================================================================
class ThreadPool {
 public:
  // =========================================================================
  // 构造函数
  // =========================================================================
  // 参数：num_threads - 工作线程数量，默认为硬件并发数
  // =========================================================================
  explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency())
      : stop_(false), active_tasks_(0) {
    if (num_threads == 0) {
      num_threads = 1;  // 至少一个线程
    }

    // 创建工作线程
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
      workers_.emplace_back(&ThreadPool::WorkerLoop, this);
    }
  }

  // =========================================================================
  // 析构函数 - 优雅关闭
  // =========================================================================
  // 停止接受新任务，等待所有已提交任务完成
  // =========================================================================
  ~ThreadPool() {
    Shutdown();
  }

  // 禁用拷贝和移动
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  // =========================================================================
  // Submit - 提交任务并获取 future
  // =========================================================================
  // 知识点：
  // 1. 模板参数推导：F 为可调用对象类型，Args 为参数类型包
  // 2. std::result_of / std::invoke_result：推导返回类型
  // 3. std::packaged_task：将任务包装，关联 future
  // 4. 完美转发：保持参数的值类别
  //
  // 返回值：std::future<返回值类型>，用于获取异步结果
  // =========================================================================
  template <typename F, typename... Args>
  auto Submit(F&& func, Args&&... args)
      -> std::future<typename std::invoke_result<F, Args...>::type> {
    // 推导返回类型
    using ReturnType = typename std::invoke_result<F, Args...>::type;

    // 将函数和参数绑定成无参可调用对象
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(func), std::forward<Args>(args)...));

    // 获取 future
    std::future<ReturnType> result = task->get_future();

    // 入队
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);

      // 检查是否已停止
      if (stop_) {
        throw std::runtime_error("Cannot submit task to stopped ThreadPool");
      }

      // 将包装后的任务加入队列
      tasks_.emplace([task]() { (*task)(); });
    }

    // 通知一个等待的工作线程
    condition_.notify_one();

    return result;
  }

  // =========================================================================
  // Shutdown - 优雅关闭线程池
  // =========================================================================
  // 1. 停止接受新任务
  // 2. 唤醒所有工作线程
  // 3. 等待所有线程结束
  // =========================================================================
  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_) {
        return;  // 已经停止
      }
      stop_ = true;
    }

    // 唤醒所有工作线程
    condition_.notify_all();

    // 等待所有工作线程结束
    for (std::thread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  // =========================================================================
  // 查询方法
  // =========================================================================
  size_t GetThreadCount() const { return workers_.size(); }

  size_t GetPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
  }

  size_t GetActiveTaskCount() const { return active_tasks_; }

  bool IsStopped() const { return stop_; }

  // =========================================================================
  // WaitForAll - 等待所有任务完成
  // =========================================================================
  void WaitForAll() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    done_condition_.wait(lock, [this]() {
      return tasks_.empty() && active_tasks_ == 0;
    });
  }

 private:
  // =========================================================================
  // WorkerLoop - 工作线程主循环
  // =========================================================================
  // 知识点：
  // 1. 使用 condition_variable::wait() 在无任务时挂起
  // 2. 谓词检查防止虚假唤醒
  // 3. 从队列取任务并执行
  // =========================================================================
  void WorkerLoop() {
    while (true) {
      std::function<void()> task;

      {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 等待条件：有任务 或 已停止
        condition_.wait(lock, [this]() {
          return stop_ || !tasks_.empty();
        });

        // 如果已停止且无任务，退出
        if (stop_ && tasks_.empty()) {
          return;
        }

        // 取出任务
        task = std::move(tasks_.front());
        tasks_.pop();

        // 增加活跃任务计数
        ++active_tasks_;
      }

      // 执行任务（在锁外执行，提高并发性）
      task();

      // 减少活跃任务计数
      --active_tasks_;

      // 通知可能在等待的 WaitForAll()
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (tasks_.empty() && active_tasks_ == 0) {
          done_condition_.notify_all();
        }
      }
    }
  }

  // 工作线程集合
  std::vector<std::thread> workers_;

  // 任务队列
  std::queue<std::function<void()>> tasks_;

  // 同步原语
  mutable std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable done_condition_;

  // 状态标志
  std::atomic<bool> stop_;
  std::atomic<size_t> active_tasks_;
};

}  // namespace w5

#endif  // W5_THREAD_POOL_THREAD_POOL_HPP_
