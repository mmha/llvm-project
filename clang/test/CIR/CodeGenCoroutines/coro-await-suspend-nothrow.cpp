// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -fcxx-exceptions -fexceptions %s -o %t-cir-eh.ll
// RUN: FileCheck --input-file=%t-cir-eh.ll %s -check-prefix=LLVM-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes -fcxx-exceptions -fexceptions %s -o %t-eh.ll
// RUN: FileCheck --input-file=%t-eh.ll %s -check-prefix=LLVM-EH

// An await_suspend wrapper call is nothrow when the suspend expression cannot
// throw and await_suspend does not return a coroutine handle, the rule of
// classic codegen, with or without exceptions. Classic codegen marks such a
// call nounwind and invokes the other ones inside an EH scope, here the
// handler that calls unhandled_exception(), and so does the lowering through
// CIR. The LLVM checks are shared.

#include "Inputs/coroutine.h"

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

struct VoidNoexcept {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

struct VoidMayThrow {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>);
  void await_resume() noexcept;
};

struct BoolNoexcept {
  bool await_ready() noexcept;
  bool await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

struct BoolMayThrow {
  bool await_ready() noexcept;
  bool await_suspend(std::coroutine_handle<>);
  void await_resume() noexcept;
};

// The coroutine split resumes the returned coroutine right after the wrapper
// call, so a handle-returning await_suspend may throw even if it is noexcept.
struct HandleNoexcept {
  bool await_ready() noexcept;
  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

// The destructor of the argument temporary may throw.
struct ThrowingDtorArg {
  ThrowingDtorArg(std::coroutine_handle<Task::promise_type>) noexcept;
  ~ThrowingDtorArg() noexcept(false);
};

struct ArgDtorMayThrow {
  bool await_ready() noexcept;
  void await_suspend(ThrowingDtorArg) noexcept;
  void await_resume() noexcept;
};

Task f() {
  co_await VoidNoexcept{};
  co_await VoidMayThrow{};
  co_await BoolNoexcept{};
  co_await BoolMayThrow{};
  co_await HandleNoexcept{};
  co_await ArgDtorMayThrow{};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z1fv()
// CIR: cir.coro.intrinsic.await.suspend.void(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__init) nothrow :
// CIR: cir.coro.intrinsic.await.suspend.void(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__await) nothrow :
// CIR: cir.coro.intrinsic.await.suspend.void(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__await2) :
// CIR: cir.coro.intrinsic.await.suspend.bool(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__await3) nothrow :
// CIR: cir.coro.intrinsic.await.suspend.bool(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__await4) :
// CIR: cir.coro.intrinsic.await.suspend.handle(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__await5) :
// CIR: cir.coro.intrinsic.await.suspend.void(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__await6) :
// CIR: cir.coro.intrinsic.await.suspend.void(%{{.*}}, %{{.*}}, @_Z1fv.__await_suspend_wrapper__final) nothrow :

// LLVM-LABEL: define dso_local void @_Z1fv()
// LLVM: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__init) #[[NOUNWIND:[0-9]+]]
// LLVM: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await) #[[NOUNWIND]]
// LLVM: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await2){{$}}
// LLVM: call i1 @llvm.coro.await.suspend.bool({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await3) #[[NOUNWIND]]
// LLVM: call i1 @llvm.coro.await.suspend.bool({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await4){{$}}
// LLVM: call void @llvm.coro.await.suspend.handle({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await5){{$}}
// LLVM: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await6){{$}}
// LLVM: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__final) #[[NOUNWIND]]
// LLVM: attributes #[[NOUNWIND]] = { nounwind }

// LLVM-EH-LABEL: define dso_local void @_Z1fv()
// LLVM-EH: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__init) #[[NOUNWIND:[0-9]+]]
// LLVM-EH: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await) #[[NOUNWIND]]
// LLVM-EH: invoke void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await2)
// LLVM-EH: call i1 @llvm.coro.await.suspend.bool({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await3) #[[NOUNWIND]]
// LLVM-EH: invoke i1 @llvm.coro.await.suspend.bool({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await4)
// LLVM-EH: invoke void @llvm.coro.await.suspend.handle({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await5)
// LLVM-EH: invoke void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__await6)
// LLVM-EH: call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z1fv.__await_suspend_wrapper__final) #[[NOUNWIND]]
// LLVM-EH: attributes #[[NOUNWIND]] = { nounwind }
