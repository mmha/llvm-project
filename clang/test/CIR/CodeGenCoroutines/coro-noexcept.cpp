// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir-eh.ll
// RUN: FileCheck --input-file=%t-cir-eh.ll %s -check-prefixes=LLVM,LLVM-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefixes=LLVM,LLVM-EH

// A noexcept coroutine. With exceptions, the body of a noexcept function is a
// cir.try that calls std::terminate when an exception escapes it, so the
// coro.id is nested in the try. The promise's alloca is emitted into the
// function entry block, ahead of the try, and the coro.id still refers to it.
// The cir.coroutine is flattened inside the try, and the try is flattened
// around it.

#include "Inputs/coroutine.h"

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

Task plain() noexcept { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z5plainv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR-NOEH-NOT: cir.try
// CIR-EH:      cir.try {
// CIR:         %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR-NEXT:    %[[ALIGN:.*]] = cir.const #cir.int<16> : !u32i
// CIR-NEXT:    %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%[[ALIGN]], %[[PROMISE_VOID]], %[[NULL]], %[[NULL]])
// CIR:         cir.coroutine initialSuspend : {
// CIR:           cir.call @_ZN4Task12promise_type17get_return_objectEv(%[[PROMISE]])
// CIR:         }, exit : {
// CIR:           cir.return
// CIR:         }
// CIR-EH:      } catch all (%[[TOKEN:.*]]: !cir.eh_token {{.*}}) {
// CIR-EH-NEXT:   cir.eh.terminate %[[TOKEN]] : !cir.eh_token
// CIR-EH-NEXT: }

// With exceptions, an exception from operator new, the only call in the ramp
// that may throw, terminates.

// LLVM-LABEL: define {{.*}} @_Z5plainv()
// LLVM-SAME:    #[[ATTRS:[0-9]+]]
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)
// LLVM-EH:      invoke {{.*}} @_Znwm(
// LLVM-EH-NEXT:   to label %{{.*}} unwind label %[[TERMINATE:.*]]
// LLVM:         call void @llvm.coro.end(ptr null, i1 false, token none)
// LLVM:         ret void
// LLVM-EH:    [[TERMINATE]]:
// LLVM-EH-NEXT: landingpad { ptr, i32 }
// LLVM-EH-NEXT:   catch ptr null
// LLVM-EH:      call void @__clang_call_terminate(

// The parameter copies are made in the try as well.

Task with_param(int n) noexcept { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z10with_parami(
// CIR:         %[[N:.*]] = cir.alloca "n" {{.*}} : !cir.ptr<!s32i>
// CIR:         %[[N_COPY:.*]] = cir.alloca "n" {{.*}} : !cir.ptr<!s32i>
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR-NOEH-NOT: cir.try
// CIR-EH:      cir.try {
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})
// CIR:         %[[N_VAL:.*]] = cir.load {{.*}} %[[N]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:    cir.store {{.*}} %[[N_VAL]], %[[N_COPY]] : !s32i, !cir.ptr<!s32i>
// CIR-NEXT:    cir.coroutine initialSuspend : {
// CIR:         }, exit : {
// CIR-EH:      } catch all
// CIR-EH-NEXT:   cir.eh.terminate

// LLVM-LABEL: define {{.*}} @_Z10with_parami(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

// A noexcept lambda coroutine, and an exception specification that depends on
// a template parameter.

void use_lambda() {
  auto l = []() noexcept -> Task { co_return; };
  l();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_ZZ10use_lambdavENK3$_0clEv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR-NOEH-NOT: cir.try
// CIR-EH:      cir.try {
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})
// CIR:         }, exit : {
// CIR-EH:      } catch all
// CIR-EH-NEXT:   cir.eh.terminate

// LLVM-LABEL: define {{.*}} @"_ZZ10use_lambdavENK3$_0clEv"(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

template <bool NoExcept> Task maybe_noexcept() noexcept(NoExcept) { co_return; }
template Task maybe_noexcept<true>();
template Task maybe_noexcept<false>();

// CIR-LABEL: cir.func {{.*}}coroutine {{.*}} @_Z14maybe_noexceptILb1EE4Taskv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR-NOEH-NOT: cir.try
// CIR-EH:      cir.try {
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})
// CIR:         }, exit : {
// CIR-EH:      } catch all
// CIR-EH-NEXT:   cir.eh.terminate

// CIR-LABEL: cir.func {{.*}}coroutine {{.*}} @_Z14maybe_noexceptILb0EE4Taskv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR-NOT:     cir.try {
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})

// LLVM-LABEL: define {{.*}} @_Z14maybe_noexceptILb1EE4Taskv(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

// LLVM-LABEL: define {{.*}} @_Z14maybe_noexceptILb0EE4Taskv(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

// LLVM: attributes #[[ATTRS]] = { {{.*}}presplitcoroutine
