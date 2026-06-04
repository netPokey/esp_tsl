#pragma once
#include <atomic>

// 统一的“共享状态包装器”。
// 设计目的：
// 1. 设备固件构建时使用原子类型，避免多执行上下文访问共享开关时出现未定义行为。
// 2. 本地原生构建或轻量测试时退化成普通值，减少平台依赖和工具链噪声。
#ifdef NATIVE_BUILD
template <typename T>
using Shared = T;
#else
template <typename T>
using Shared = std::atomic<T>;
#endif