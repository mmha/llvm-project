// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -O2 -disable-llvm-passes -emit-llvm %s -o - | opt -passes='default<O0>,default<O2>' -S | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -O2 -emit-llvm %s -o - | FileCheck %s

// Like classic CodeGen (clang/test/CodeGenCoroutines/coro-gro4.cpp), the
// conversion and the destruction of the result of get_return_object() stay in
// the ramp function after the coroutine split: the gro region runs only when
// llvm.coro.is_in_ramp returns true.

#include "Inputs/coroutine.h"

extern "C" void gro_destroy() noexcept;

struct task {
  struct promise_type {
    task get_return_object() noexcept { return task{this}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() {}
  };

  task(promise_type *promise) : m_promise(promise) {}
  task(task &&o) noexcept : m_promise(o.m_promise) { o.m_promise = nullptr; }
  task(const task&) = delete;
  task& operator=(const task&) = delete;
  task& operator=(task&&) = delete;

  ~task() { gro_destroy(); }

  promise_type *m_promise;
};

struct wrapper {
  using promise_type = task::promise_type;

  wrapper(task &&t) noexcept : m_task(static_cast<task&&>(t)) {}

  task m_task;
};

wrapper fn() { co_return; }

// CHECK: define dso_local void @_Z2fnv
// CHECK: call void @gro_destroy()
// CHECK: ret void

// CHECK: define internal void @_Z2fnv.resume
// CHECK-NOT: call void @gro_destroy()

// CHECK: define internal void @_Z2fnv.destroy
// CHECK-NOT: call void @gro_destroy()
