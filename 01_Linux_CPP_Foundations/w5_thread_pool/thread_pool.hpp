// Copyright 2026 Edge-AI-Genesis
//
// ============================================================================
// 文件功能：通用线程池架构 (C++20/23 版本)
// ============================================================================
//
// 本文件实现了一个高性能、类型安全的线程池，专为边缘侧 AI 推理场景设计。
// 相比 C++11/14 的传统实现，本版本充分利用了 C++20/23 的现代特性：
//
// 【C++20 核心特性应用】
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ 特性                    │ 替代的传统方案            │ 优势              │
// ├─────────────────────────┼──────────────────────────┼──────────────────┤
// │ std::jthread            │ std::thread + 手动 join  │ RAII 自动汇合    │
// │ std::stop_token         │ std::atomic<bool> stop_  │ 标准化协同中断   │
// │ std::condition_var_any  │ condition_variable       │ 支持 stop_token  │
// │ requires (Concepts)     │ std::enable_if (SFINAE)  │ 清晰的编译错误   │
// │ alignas(64)             │ 无                       │ 消除伪共享       │
// └─────────────────────────────────────────────────────────────────────────┘
//
// 【学习路径建议】
// 如果你从 C++11/14 过渡而来，建议按以下顺序阅读：
// 1. 先看 Submit() 函数 —— 理解 packaged_task + future 机制
// 2. 再看构造函数 —— 理解 jthread 如何启动工作线程
// 3. 然后看 WorkerLoop() —— 理解 stop_token + condition_variable_any 联动
// 4. 最后看成员变量布局 —— 理解 alignas(64) 消除伪共享的原理
//
// ============================================================================

#ifndef W5_THREAD_POOL_THREAD_POOL_HPP_
#define W5_THREAD_POOL_THREAD_POOL_HPP_

// ============================================================================
// 头文件包含 —— 按字母顺序排列（Google 风格）
// ============================================================================
#include <atomic>              // std::atomic —— 无锁原子操作
#include <concepts>            // C++20 Concepts —— 模板约束
#include <condition_variable>  // condition_variable_any —— 支持 stop_token
#include <format>              // C++20 std::format —— 类型安全格式化（保留备用）
#include <functional>          // std::function, std::bind —— 可调用对象包装
#include <future>              // std::future, std::packaged_task —— 异步结果
#include <memory>              // std::shared_ptr, std::make_shared
#include <mutex>               // std::mutex, std::lock_guard, std::unique_lock
#include <queue>               // std::queue —— 任务队列
#include <stdexcept>           // std::runtime_error —— 异常类型
#include <stop_token>          // C++20 std::stop_token —— 协同中断机制
#include <thread>              // std::jthread, std::thread::hardware_concurrency
#include <type_traits>         // std::invoke_result_t —— 返回类型推导
#include <vector>              // std::vector —— 存储工作线程

namespace w5 {

// ============================================================================
// ThreadPool 类 —— 通用线程池 (C++20/23)
// ============================================================================
//
// 【设计目标】
// 1. 支持提交任意可调用对象（函数、Lambda、std::function、成员函数）
// 2. 返回 std::future 以获取异步执行结果
// 3. 线程在无任务时低功耗挂起（CPU 占用趋近 0%）
// 4. 使用 std::jthread + stop_token 实现 RAII 优雅关闭
// 5. 缓存行对齐避免高并发下的伪共享
//
// 【使用示例】
// ```cpp
// w5::ThreadPool pool(4);  // 创建 4 线程的池
//
// // 提交任务并获取 future
// auto future = pool.Submit([](int x) { return x * 2; }, 21);
// int result = future.get();  // result = 42
//
// // 提交可中断任务
// auto f2 = pool.SubmitWithToken([](std::stop_token token) {
//   while (!token.stop_requested()) { /* work */ }
//   return -1;
// });
// pool.Shutdown();  // 触发中断
// ```
// ============================================================================
class ThreadPool {
 public:
  // ==========================================================================
  // 构造函数
  // ==========================================================================
  /**
   * @brief 创建线程池并启动指定数量的工作线程。
   *
   * 【C++20 特性：std::jthread 的 RAII 自动汇合】
   *
   * 传统 std::thread 的问题：
   * - 析构时如果线程仍在运行，程序会调用 std::terminate() 崩溃
   * - 必须手动调用 join() 或 detach()，容易遗漏
   *
   * std::jthread 的优势：
   * - 析构时自动调用 request_stop() 请求线程停止
   * - 然后自动调用 join() 等待线程结束
   * - 完全符合 RAII（资源获取即初始化）原则
   *
   * @param num_threads 工作线程数量，默认为 CPU 核心数。
   *                    如果传入 0，会自动调整为 1。
   */
  explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency())
      : stop_source_(),        // 初始化停止信号源（默认未请求停止）
        active_tasks_(0) {     // 初始化活跃任务计数为 0
    // 防御性编程：确保至少有一个工作线程
    if (num_threads == 0) {
      num_threads = 1;
    }

    // 预分配 vector 空间，避免多次扩容带来的内存分配开销
    workers_.reserve(num_threads);

    // 启动 num_threads 个工作线程
    for (size_t i = 0; i < num_threads; ++i) {
      // ════════════════════════════════════════════════════════════════════
      // 【关键点】std::jthread 启动时接收 std::stop_token
      // ════════════════════════════════════════════════════════════════════
      //
      // 当 lambda 的第一个参数类型是 std::stop_token 时，jthread 会自动：
      // 1. 从自身的 stop_source 获取一个 stop_token
      // 2. 将其作为第一个参数传递给 lambda
      //
      // 这是 C++20 的"stop_token 协议"，无需手动传递！
      //
      // 【与 C++11 对比】
      // C++11: std::thread([this]() { WorkerLoop(); });
      //        需要额外的 std::atomic<bool> stop_ 成员变量
      //
      // C++20: std::jthread([this](std::stop_token st) { WorkerLoop(st); });
      //        stop_token 由 jthread 自动管理
      // ════════════════════════════════════════════════════════════════════
      workers_.emplace_back([this](std::stop_token stop_token) {
        WorkerLoop(stop_token);
      });
    }
  }

  // ==========================================================================
  // 析构函数 —— RAII 优雅关闭
  // ==========================================================================
  /**
   * @brief 析构时优雅关闭线程池，等待所有任务完成。
   *
   * 【C++20 std::jthread 的析构行为】
   *
   * std::jthread 析构时会自动执行：
   * 1. 调用 get_stop_source().request_stop() —— 通知线程应该停止
   * 2. 调用 join() —— 等待线程执行完毕
   *
   * 因此，即使不显式调用 Shutdown()，析构函数也能保证：
   * - 所有提交的任务都会被处理完
   * - 不会有"悬空线程"（dangling thread）问题
   *
   * 我们在这里显式调用 Shutdown() 是为了：
   * - 明确表达关闭意图
   * - 在析构前统一发送停止信号
   */
  ~ThreadPool() {
    Shutdown();
  }

  // ==========================================================================
  // 禁用拷贝和移动
  // ==========================================================================
  // 【设计原因】
  // 线程池管理着活跃的工作线程和共享的任务队列。
  // 拷贝会导致多个对象管理同一组线程，造成数据竞争和双重 join。
  // 移动虽然技术上可行，但语义复杂且容易出错，这里选择禁用。
  // ==========================================================================
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  // ==========================================================================
  // Submit —— 提交任务并获取 future
  // ==========================================================================
  /**
   * @brief 提交一个可调用对象到线程池，返回 future 用于获取结果。
   *
   * 【C++20 Concepts 语法解析】
   *
   * `requires std::invocable<F, Args...>` 是 C++20 的"约束子句"。
   *
   * 它的作用是在编译期检查：
   * - F 类型必须可以用 Args... 参数调用
   * - 如果检查失败，编译器会给出清晰的错误信息
   *
   * 【与 C++11 SFINAE 对比】
   *
   * C++11 的写法（晦涩难懂）：
   * ```cpp
   * template <typename F, typename... Args,
   *           typename = std::enable_if_t<
   *               std::is_invocable_v<F, Args...>>>
   * auto Submit(F&& func, Args&&... args) { ... }
   * ```
   *
   * C++20 的写法（清晰直观）：
   * ```cpp
   * template <typename F, typename... Args>
   *   requires std::invocable<F, Args...>
   * auto Submit(F&& func, Args&&... args) { ... }
   * ```
   *
   * 当约束不满足时，C++20 编译器错误信息示例：
   * "error: constraints not satisfied for 'Submit'"
   * "note: 'std::invocable<int, char*>' evaluated to false"
   *
   * 而 C++11 SFINAE 错误信息通常是一大堆模板展开，难以定位问题。
   *
   * @tparam F    可调用对象类型（函数、Lambda、std::function 等）。
   *              必须满足 std::invocable<F, Args...> 约束。
   * @tparam Args 调用参数类型包。
   *
   * @param func 要执行的可调用对象。
   * @param args 传递给 func 的参数。
   *
   * @return std::future<ReturnType> 用于获取异步执行结果。
   *         调用 future.get() 会阻塞直到任务完成。
   *         如果任务抛出异常，get() 会重新抛出该异常。
   *
   * @throws std::runtime_error 如果线程池已停止，禁止提交新任务。
   *
   * @note 线程安全：可以从多个线程同时调用此函数。
   */
  template <typename F, typename... Args>
    requires std::invocable<F, Args...>  // C++20 Concepts 约束
  auto Submit(F&& func, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>> {
    // ════════════════════════════════════════════════════════════════════════
    // 步骤 1：推导返回类型
    // ════════════════════════════════════════════════════════════════════════
    // std::invoke_result_t<F, Args...> 会推导出 func(args...) 的返回类型。
    // 例如：如果 func 是 [](int x) { return x * 2; }，args 是 (21)，
    //       那么 ReturnType 就是 int。
    using ReturnType = std::invoke_result_t<F, Args...>;

    // ════════════════════════════════════════════════════════════════════════
    // 步骤 2：创建 packaged_task 包装任务
    // ════════════════════════════════════════════════════════════════════════
    //
    // 【为什么需要 std::packaged_task？】
    //
    // 问题：我们需要在工作线程中执行 func(args...)，并在主线程获取返回值。
    //       但 std::function<void()> 无法捕获返回值。
    //
    // 解决：std::packaged_task 会：
    //       1. 包装可调用对象
    //       2. 关联一个 std::future
    //       3. 执行时将返回值/异常存入 future
    //
    // 【为什么用 std::bind？】
    //
    // std::packaged_task<ReturnType()> 要求无参数的可调用对象。
    // 但用户传入的是 func + args，我们用 std::bind 把它们"绑定"成无参函数。
    //
    // 【为什么用 shared_ptr？】
    //
    // packaged_task 是只能移动（move-only）的类型，不能拷贝。
    // 但 std::function<void()> 要求可拷贝。
    // 解决方案：用 shared_ptr 包装，拷贝的是指针，不是 task 本身。
    // ════════════════════════════════════════════════════════════════════════
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(func), std::forward<Args>(args)...));

    // ════════════════════════════════════════════════════════════════════════
    // 步骤 3：获取关联的 future
    // ════════════════════════════════════════════════════════════════════════
    // future 和 packaged_task 共享一个"共享状态"（shared state）。
    // 当 task 执行完毕时，结果会写入共享状态。
    // 当调用 future.get() 时，会从共享状态读取结果。
    std::future<ReturnType> result = task->get_future();

    // ════════════════════════════════════════════════════════════════════════
    // 步骤 4：将任务加入队列（临界区）
    // ════════════════════════════════════════════════════════════════════════
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);

      // 检查线程池是否已停止
      // 如果已停止，禁止提交新任务，抛出异常
      if (stop_source_.stop_requested()) {
        throw std::runtime_error("Cannot submit task to stopped ThreadPool");
      }

      // 将 packaged_task 包装成 std::function<void()>
      // lambda 捕获 shared_ptr<packaged_task>，执行时调用 (*task)()
      tasks_.emplace([task]() { (*task)(); });
    }
    // lock_guard 析构，自动释放锁

    // ════════════════════════════════════════════════════════════════════════
    // 步骤 5：通知一个等待的工作线程
    // ════════════════════════════════════════════════════════════════════════
    // notify_one() 会唤醒一个在 condition_.wait() 上阻塞的线程。
    // 被唤醒的线程会重新检查条件，发现队列非空，取出任务执行。
    condition_.notify_one();

    return result;
  }

  // ==========================================================================
  // SubmitWithToken —— 提交可中断任务
  // ==========================================================================
  /**
   * @brief 提交一个可响应停止请求的任务。
   *
   * 与 Submit() 不同，此函数提交的任务会接收一个 std::stop_token 参数。
   * 任务内部可以检查 stop_token.stop_requested() 来实现优雅中断。
   *
   * 【使用场景】
   * - 长时间运行的推理任务
   * - 需要在 Shutdown() 时快速响应停止的任务
   * - 循环处理任务（如视频帧处理）
   *
   * 【代码示例】
   * ```cpp
   * auto future = pool.SubmitWithToken([](std::stop_token token) -> int {
   *   for (int i = 0; i < 1000000; ++i) {
   *     if (token.stop_requested()) {
   *       return -1;  // 被中断，提前返回
   *     }
   *     // 执行一帧处理...
   *   }
   *   return 0;  // 正常完成
   * });
   *
   * pool.Shutdown();  // 触发 stop_requested()
   * int result = future.get();  // 可能是 -1（被中断）或 0（正常完成）
   * ```
   *
   * @tparam F 可调用对象类型，签名必须是 ReturnType(std::stop_token)。
   *
   * @param func 要执行的可调用对象，接收 std::stop_token 作为唯一参数。
   *
   * @return std::future<ReturnType> 用于获取异步执行结果。
   *
   * @throws std::runtime_error 如果线程池已停止。
   */
  template <typename F>
    requires std::invocable<F, std::stop_token>  // 约束：F 必须接受 stop_token
  auto SubmitWithToken(F&& func)
      -> std::future<std::invoke_result_t<F, std::stop_token>> {
    using ReturnType = std::invoke_result_t<F, std::stop_token>;

    // 创建 packaged_task，内部 lambda 会获取线程池的 stop_token 并传给用户任务
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [this, f = std::forward<F>(func)]() {
          // 从线程池的 stop_source_ 获取 token，传递给用户任务
          return f(stop_source_.get_token());
        });

    std::future<ReturnType> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);

      if (stop_source_.stop_requested()) {
        throw std::runtime_error("Cannot submit task to stopped ThreadPool");
      }

      tasks_.emplace([task]() { (*task)(); });
    }

    condition_.notify_one();
    return result;
  }

  // ==========================================================================
  // Shutdown —— 优雅关闭线程池
  // ==========================================================================
  /**
   * @brief 请求停止所有工作线程，并等待它们完成。
   *
   * 【关闭流程】
   * 1. 调用 stop_source_.request_stop() 设置停止标志
   * 2. 调用 condition_.notify_all() 唤醒所有等待的线程
   * 3. workers_.clear() 析构所有 jthread，触发自动 join
   *
   * 【与 C++11 对比】
   *
   * C++11 需要手动管理：
   * ```cpp
   * stop_ = true;  // 原子标志
   * condition_.notify_all();
   * for (auto& t : workers_) { t.join(); }
   * ```
   *
   * C++20 简化为：
   * ```cpp
   * stop_source_.request_stop();  // 自动通知
   * condition_.notify_all();       // 确保立即唤醒
   * workers_.clear();              // jthread 析构时自动 join
   * ```
   *
   * @note 调用后，Submit() 会抛出异常。
   * @note 多次调用是安全的（幂等操作）。
   */
  void Shutdown() {
    // 请求停止 —— 所有持有此 stop_source 的 stop_token 会看到 stop_requested()
    stop_source_.request_stop();

    // 唤醒所有等待的工作线程
    // condition_variable_any::wait() 会在 stop_requested() 后自动返回
    // 但显式 notify_all 可以加速唤醒
    condition_.notify_all();

    // 清空 workers_ 会依次析构每个 jthread
    // jthread 析构时会自动 join，等待线程执行完毕
    workers_.clear();
  }

  // ==========================================================================
  // 查询方法
  // ==========================================================================

  /**
   * @brief 获取工作线程数量。
   * @return 当前线程池中的工作线程数。
   */
  size_t GetThreadCount() const { return workers_.size(); }

  /**
   * @brief 获取队列中待处理的任务数量。
   * @return 尚未被工作线程取走的任务数。
   * @note 线程安全。
   */
  size_t GetPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
  }

  /**
   * @brief 获取正在执行的任务数量。
   * @return 当前正在工作线程中执行的任务数。
   */
  size_t GetActiveTaskCount() const { return active_tasks_.load(); }

  /**
   * @brief 检查线程池是否已停止。
   * @return 如果已调用 Shutdown() 返回 true。
   */
  bool IsStopped() const { return stop_source_.stop_requested(); }

  /**
   * @brief 获取线程池的 stop_token。
   * @return 用于外部检查线程池停止状态的 token。
   */
  std::stop_token GetStopToken() const { return stop_source_.get_token(); }

  // ==========================================================================
  // WaitForAll —— 等待所有任务完成
  // ==========================================================================
  /**
   * @brief 阻塞当前线程，直到所有任务（包括正在执行的）完成。
   *
   * 【使用场景】
   * - 需要同步点时（如批量处理完成后汇总结果）
   * - 在 Shutdown() 前确保任务完成
   *
   * @note 此函数不会阻止新任务提交。
   *       如果需要"无新任务 + 等待完成"，应先设置标志阻止提交。
   */
  void WaitForAll() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    // 等待条件：队列为空 且 没有正在执行的任务
    done_condition_.wait(lock, [this]() {
      return tasks_.empty() && active_tasks_.load() == 0;
    });
  }

 private:
  // ==========================================================================
  // WorkerLoop —— 工作线程主循环
  // ==========================================================================
  //
  // 【C++20 核心：condition_variable_any + stop_token 联动】
  //
  // 传统 C++11 的 wait 写法：
  // ```cpp
  // condition_.wait(lock, [this]() {
  //   return stop_ || !tasks_.empty();  // 需要检查 stop_ 标志
  // });
  // ```
  //
  // C++20 使用 condition_variable_any::wait(lock, stop_token, predicate)：
  // ```cpp
  // condition_.wait(lock, stop_token, [this]() {
  //   return !tasks_.empty();  // 不需要检查 stop，由 stop_token 处理
  // });
  // ```
  //
  // 【stop_token + wait 的工作原理】
  //
  // 当调用 wait(lock, stop_token, predicate) 时，内部会：
  // 1. 注册一个 stop_callback 到 stop_token
  // 2. 当 stop_token.stop_requested() 变为 true 时，callback 自动调用 notify_all()
  // 3. wait 被唤醒后检查 predicate，即使 predicate 为 false 也会因 stop 返回
  //
  // 这意味着：不需要在 Shutdown() 中手动 notify_all()！
  // （但我们还是加了，以加速唤醒速度）
  //
  // ==========================================================================
  void WorkerLoop(std::stop_token stop_token) {
    // ════════════════════════════════════════════════════════════════════════
    // 主循环：持续处理任务，直到收到停止请求
    // ════════════════════════════════════════════════════════════════════════
    while (!stop_token.stop_requested()) {
      std::function<void()> task;

      // ──────────────────────────────────────────────────────────────────────
      // 阶段 A：获取任务（临界区）
      // ──────────────────────────────────────────────────────────────────────
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // ════════════════════════════════════════════════════════════════════
        // 【核心】condition_variable_any::wait 的三参数重载
        // ════════════════════════════════════════════════════════════════════
        //
        // 函数签名：
        // bool wait(Lock& lock, stop_token stoken, Predicate pred);
        //
        // 行为：
        // 1. 如果 pred() 为 true，立即返回 true
        // 2. 如果 stoken.stop_requested()，立即返回 pred()
        // 3. 否则，阻塞等待，直到被 notify 或 stop_requested
        //
        // 返回值：
        // - true: 因为 pred() 为 true 而返回
        // - false: 因为 stop_requested() 而返回（此时 pred() 可能为 false）
        //
        // 【为什么不用普通 condition_variable？】
        // std::condition_variable 的 wait 只有两参数版本，不支持 stop_token。
        // std::condition_variable_any 才有三参数版本。
        // ════════════════════════════════════════════════════════════════════
        condition_.wait(lock, stop_token, [this]() {
          return !tasks_.empty();
        });

        // 检查是否因停止请求而唤醒（队列仍为空）
        if (stop_token.stop_requested() && tasks_.empty()) {
          return;  // 退出线程
        }

        // 处理边界情况：假唤醒（spurious wakeup）导致队列仍为空
        if (tasks_.empty()) {
          continue;  // 重新进入循环
        }

        // 取出队首任务（移动语义，避免拷贝）
        task = std::move(tasks_.front());
        tasks_.pop();

        // 增加活跃任务计数（原子操作，无锁）
        ++active_tasks_;
      }
      // unique_lock 析构，释放锁

      // ──────────────────────────────────────────────────────────────────────
      // 阶段 B：执行任务（在锁外执行！）
      // ──────────────────────────────────────────────────────────────────────
      //
      // 【重要】任务执行必须在锁外进行！
      //
      // 如果在锁内执行任务：
      // - 其他线程无法获取锁、无法提交新任务
      // - 整个线程池变成串行执行，失去并发优势
      //
      // 【异常安全】
      // packaged_task 内部会捕获任务抛出的异常，存入 future。
      // 因此这里的 task() 不会抛出异常到 WorkerLoop 中。
      // ──────────────────────────────────────────────────────────────────────
      task();

      // ──────────────────────────────────────────────────────────────────────
      // 阶段 C：更新计数器并通知等待者
      // ──────────────────────────────────────────────────────────────────────
      --active_tasks_;  // 原子操作

      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // 如果队列为空且无活跃任务，通知 WaitForAll()
        if (tasks_.empty() && active_tasks_.load() == 0) {
          done_condition_.notify_all();
        }
      }
    }

    // ════════════════════════════════════════════════════════════════════════
    // 退出前：处理剩余任务（优雅关闭）
    // ════════════════════════════════════════════════════════════════════════
    //
    // 即使收到停止请求，我们仍然处理完队列中的剩余任务。
    // 这是"优雅关闭"的核心：不丢弃已提交的任务。
    //
    // 【设计权衡】
    // - 如果需要"立即停止"（丢弃任务），可以在此处直接 return。
    // - 当前实现选择"完成所有任务后停止"。
    // ════════════════════════════════════════════════════════════════════════
    while (true) {
      std::function<void()> task;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (tasks_.empty()) {
          break;  // 队列为空，退出循环
        }
        task = std::move(tasks_.front());
        tasks_.pop();
        ++active_tasks_;
      }

      task();

      --active_tasks_;

      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (tasks_.empty() && active_tasks_.load() == 0) {
          done_condition_.notify_all();
        }
      }
    }
  }

  // ==========================================================================
  // 成员变量布局 —— 按访问频率分区，消除伪共享
  // ==========================================================================
  //
  // 【什么是伪共享（False Sharing）？】
  //
  // 现代 CPU 以"缓存行"（Cache Line）为单位读写内存，通常是 64 字节。
  // 如果两个变量位于同一缓存行，即使它们逻辑上无关：
  // - 线程 A 修改变量 X
  // - CPU 会使整个缓存行失效
  // - 线程 B 访问变量 Y 时，需要重新从内存加载
  //
  // 这就是"伪共享"——看起来没有共享，实际上因缓存行产生了冲突。
  //
  // 【性能影响】
  // 在高并发场景下，伪共享可能导致：
  // - 缓存命中率大幅下降
  // - 性能下降 2-10 倍（取决于访问频率）
  //
  // 【解决方案】
  // 使用 alignas(64) 将高频访问的变量对齐到 64 字节边界，
  // 并添加 padding 确保变量独占一个缓存行。
  //
  // ==========================================================================

  // --- 冷数据区 (Cold Data) ---
  // 初始化后很少修改，不需要特殊对齐
  std::vector<std::jthread> workers_;  // 工作线程集合

  // --- 温数据区 (Warm Data) ---
  // 中等频率访问，互斥锁保护，不需要特殊对齐
  std::queue<std::function<void()>> tasks_;  // 任务队列
  mutable std::mutex queue_mutex_;           // 队列锁（mutable 允许 const 方法中加锁）
  std::condition_variable_any condition_;    // 任务到来/停止通知
  std::condition_variable done_condition_;   // 任务完成通知

  // --- 热数据区 (Hot Data) ---
  // 高频访问，需要缓存行隔离，彻底消除伪共享

  // ════════════════════════════════════════════════════════════════════════
  // 【alignas(64) 详解】
  // ════════════════════════════════════════════════════════════════════════
  //
  // alignas(64) 的作用：
  // 1. 告诉编译器将此变量的起始地址对齐到 64 字节边界
  // 2. 这通常等于一个缓存行的大小
  //
  // padding 数组的作用：
  // 1. 确保变量后面也填充满 64 字节
  // 2. 防止后面的变量"挤进"同一个缓存行
  //
  // 效果：每个热点变量独占一个完整的缓存行，彻底隔离。
  //
  // 【内存布局示意】
  // ┌────────────────────────────────────────────────────────────┐
  // │ Cache Line 0 (64 bytes)                                   │
  // │ ┌──────────────────┬─────────────────────────────────────┐│
  // │ │ stop_source_     │ padding_stop_                       ││
  // │ │ (约 8-16 bytes)  │ (填充至 64 bytes)                   ││
  // │ └──────────────────┴─────────────────────────────────────┘│
  // ├────────────────────────────────────────────────────────────┤
  // │ Cache Line 1 (64 bytes)                                   │
  // │ ┌──────────────────┬─────────────────────────────────────┐│
  // │ │ active_tasks_    │ padding_active_                     ││
  // │ │ (8 bytes)        │ (填充至 64 bytes)                   ││
  // │ └──────────────────┴─────────────────────────────────────┘│
  // └────────────────────────────────────────────────────────────┘
  // ════════════════════════════════════════════════════════════════════════

  // stop_source_ 被多线程频繁读取（每次 stop_requested() 检查）
  alignas(64) std::stop_source stop_source_;
  char padding_stop_[64 - sizeof(std::stop_source)];  // 后向填充

  // active_tasks_ 每次任务执行时 +1/-1，极高频访问
  alignas(64) std::atomic<size_t> active_tasks_;
  char padding_active_[64 - sizeof(std::atomic<size_t>)];  // 后向填充
};

}  // namespace w5

#endif  // W5_THREAD_POOL_THREAD_POOL_HPP_
