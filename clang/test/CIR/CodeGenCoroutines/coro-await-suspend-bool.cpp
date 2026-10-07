// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -Wno-coroutine-missing-unhandled-exception -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -Wno-coroutine-missing-unhandled-exception -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes -Wno-coroutine-missing-unhandled-exception %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

#include "Inputs/coroutine.h"

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

struct BoolAwaiter {
  bool await_ready() { return false; }
  bool await_suspend(std::coroutine_handle<>) { return false; }
  void await_resume() {}
};

// CIR-LABEL: cir.func coroutine {{.*}} @_Z15await_bool_vetov
// LLVM-LABEL: define dso_local void @_Z15await_bool_vetov
// OGCG-LABEL: define dso_local void @_Z15await_bool_vetov
Task await_bool_veto() {
  // CIR: %[[AWAITER_ADDR:.*]] = cir.alloca "ref.tmp1" {{.*}} : !cir.ptr<!rec_BoolAwaiter>
  // CIR: %[[FRAME:.*]] = cir.coro.intrinsic.begin

  // The result of the await_suspend wrapper decides whether the coroutine
  // stays suspended.

  // CIR: cir.await(user, ready : {
  // CIR:   %[[READY:.*]] = cir.call @_ZN11BoolAwaiter11await_readyEv(%[[AWAITER_ADDR]]) : (!cir.ptr<!rec_BoolAwaiter>{{.*}}) -> (!cir.bool{{.*}})
  // CIR:   cir.condition(%[[READY]])
  // CIR: }, suspend : {
  // CIR:   %[[AWAITER:.*]] = cir.cast bitcast %[[AWAITER_ADDR]] : !cir.ptr<!rec_BoolAwaiter> -> !cir.ptr<!void>
  // CIR:   %[[SUSPEND_RET:.*]] = cir.coro.intrinsic.await.suspend.bool(%[[AWAITER]], %[[FRAME]], @_Z15await_bool_vetov.__await_suspend_wrapper__await) : (!cir.ptr<!void>, !cir.ptr<!void>) -> !cir.bool
  // CIR:   cir.condition(%[[SUSPEND_RET]])
  // CIR: }, resume : {
  // CIR:   cir.call @_ZN11BoolAwaiter12await_resumeEv(%[[AWAITER_ADDR]]) : (!cir.ptr<!rec_BoolAwaiter>{{.*}}) -> ()
  // CIR:   cir.yield
  // CIR: },)

  // When the await_suspend wrapper returns false, the coroutine continues with
  // await_resume right away. Otherwise it suspends.

  // LLVM:   %[[READY_RES:.*]] = call noundef zeroext i1 @_ZN11BoolAwaiter11await_readyEv(ptr {{.*}})
  // LLVM:   br i1 %[[READY_RES]], label %[[AWAIT_READY:.*]], label %[[AWAIT_SUSPEND:.*]]
  // LLVM: [[AWAIT_SUSPEND]]:
  // LLVM:   %[[SAVE:.*]] = call token @llvm.coro.save(ptr null)
  // LLVM:   %[[SUSP_RET:.*]] = call i1 @llvm.coro.await.suspend.bool(ptr {{.*}}, ptr {{.*}}, ptr @_Z15await_bool_vetov.__await_suspend_wrapper__await)
  // LLVM:   br i1 %[[SUSP_RET]], label %[[REAL_SUSPEND:.*]], label %[[AWAIT_READY]]
  // LLVM: [[REAL_SUSPEND]]:
  // LLVM:   %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %[[SAVE]], i1 false)
  // LLVM:   switch i8 %[[SUSPEND]], label %{{.*}} [
  // LLVM:     i8 0, label %[[AWAIT_READY]]
  // LLVM:     i8 1, label %{{.*}}
  // LLVM:   ]
  // LLVM: [[AWAIT_READY]]:
  // LLVM:   call void @_ZN11BoolAwaiter12await_resumeEv(

  // OGCG:   %[[READY_RES:.*]] = call noundef zeroext i1 @_ZN11BoolAwaiter11await_readyEv(ptr {{.*}})
  // OGCG:   br i1 %[[READY_RES]], label %[[AWAIT_READY_DEST:.*]], label %[[AWAIT_SUSPEND:.*]]
  // OGCG: [[AWAIT_SUSPEND]]:
  // OGCG:   %[[SUSP_RET:.*]] = call i1 @llvm.coro.await.suspend.bool(ptr {{.*}}, ptr {{.*}}, ptr @_Z15await_bool_vetov.__await_suspend_wrapper__await)
  // OGCG:   br i1 %[[SUSP_RET]], label %[[REAL_SUSPEND:.*]], label %[[AWAIT_READY_DEST]]
  // OGCG: [[REAL_SUSPEND]]:
  // OGCG:   %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
  // OGCG:   switch i8 %[[SUSPEND]], label %{{.*}} [
  // OGCG:     i8 0, label %[[AWAIT_READY_DEST]]
  // OGCG:     i8 1, label %{{.*}}
  // OGCG:   ]
  co_await BoolAwaiter{};
}

// The wrapper returns the result of await_suspend.

// CIR: cir.func always_inline internal private @_Z15await_bool_vetov.__await_suspend_wrapper__await(%[[AWAITER_ARG:[^:]*]]: !cir.ptr<!void> {llvm.nonnull, llvm.noundef} {{.*}}, %[[FRAME_ARG:[^:]*]]: !cir.ptr<!void> {llvm.noundef} {{.*}}) -> (!cir.bool {llvm.noundef, llvm.zeroext})
// CIR:   %[[AWAITER_ARG_ADDR:.*]] = cir.alloca "" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[FRAME_ARG_ADDR:.*]] = cir.alloca "" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[RETVAL:.*]] = cir.alloca "__retval" {{.*}} : !cir.ptr<!cir.bool>
// CIR:   cir.store %[[AWAITER_ARG]], %[[AWAITER_ARG_ADDR]]
// CIR:   cir.store %[[FRAME_ARG]], %[[FRAME_ARG_ADDR]]
// CIR:   %[[AWAITER_VOID_PTR:.*]] = cir.load{{.*}} %[[AWAITER_ARG_ADDR]]
// CIR:   %[[AWAITER_PTR:.*]] = cir.cast bitcast %[[AWAITER_VOID_PTR]] : !cir.ptr<!void> -> !cir.ptr<!rec_BoolAwaiter>
// CIR:   %[[FRAME_PTR:.*]] = cir.load{{.*}} %[[FRAME_ARG_ADDR]]
// CIR:   cir.call @_ZNSt16coroutine_handleIN4Task12promise_typeEE12from_addressEPv(%[[FRAME_PTR]])
// CIR:   %[[RES:.*]] = cir.call @_ZN11BoolAwaiter13await_suspendESt16coroutine_handleIvE(%[[AWAITER_PTR]]) : (!cir.ptr<!rec_BoolAwaiter>{{.*}}) -> (!cir.bool{{.*}})
// CIR:   cir.store{{.*}} %[[RES]], %[[RETVAL]]
// CIR:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR:   cir.return %[[RET]] : !cir.bool

// LLVM: define internal noundef zeroext i1 @_Z15await_bool_vetov.__await_suspend_wrapper__await(ptr noundef nonnull %{{.*}}, ptr noundef %{{.*}})
// LLVM:   %[[RES:.*]] = call noundef zeroext i1 @_ZN11BoolAwaiter13await_suspendESt16coroutine_handleIvE(
// LLVM:   ret i1

// OGCG: define internal noundef zeroext i1 @_Z15await_bool_vetov.__await_suspend_wrapper__await(ptr noundef nonnull %[[AWAITER_ARG:.*]], ptr noundef %[[FRAME_ARG:.*]])
// OGCG:   %[[AWAITER_ARG_ADDR:.*]] = alloca ptr
// OGCG:   %[[FRAME_ARG_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %[[AWAITER_ARG]], ptr %[[AWAITER_ARG_ADDR]]
// OGCG:   store ptr %[[FRAME_ARG]], ptr %[[FRAME_ARG_ADDR]]
// OGCG:   %[[AWAITER_PTR:.*]] = load ptr, ptr %[[AWAITER_ARG_ADDR]]
// OGCG:   %[[FRAME_PTR:.*]] = load ptr, ptr %[[FRAME_ARG_ADDR]]
// OGCG:   call void @_ZNSt16coroutine_handleIN4Task12promise_typeEE12from_addressEPv(ptr noundef %[[FRAME_PTR]])
// OGCG:   %[[RES:.*]] = call noundef zeroext i1 @_ZN11BoolAwaiter13await_suspendESt16coroutine_handleIvE(ptr noundef nonnull align 1 dereferenceable(1) %[[AWAITER_PTR]])
// OGCG:   ret i1 %[[RES]]
