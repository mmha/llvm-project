// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// The promise operand of coro.id names the promise alloca, as in classic
// CodeGen. The coro.id is created before the promise is emitted, so the operand
// is set once the promise exists. The promise's alloca is emitted into the
// function entry block, ahead of the coro.id, and the cast of its address is
// emitted right before the coro.id. coro-noexcept.cpp covers a coro.id nested
// in the cir.try of a noexcept coroutine.

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

Task plain() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z5plainv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR:         %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR-NEXT:    %[[ALIGN:.*]] = cir.const #cir.int<16> : !u32i
// CIR-NEXT:    %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%[[ALIGN]], %[[PROMISE_VOID]], %[[NULL]], %[[NULL]])
// CIR:         cir.coroutine initialSuspend : {
// CIR:           cir.call @_ZN4Task12promise_type17get_return_objectEv(%[[PROMISE]])

// LLVM-LABEL: define {{.*}} @_Z5plainv(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

// A promise constructor that takes the coroutine's parameters is passed the
// parameter copies, which are emitted before the promise.

struct ParamTask {
  struct promise_type {
    promise_type(int);
    ParamTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

ParamTask with_param(int n) { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z10with_parami(
// CIR:         cir.alloca "n" {{.*}} : !cir.ptr<!s32i>
// CIR:         %[[N_COPY:.*]] = cir.alloca "n" {{.*}} : !cir.ptr<!s32i>
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_ParamTask3A3Apromise_type>
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_ParamTask3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})
// CIR:         cir.coroutine initialSuspend : {
// CIR-NEXT:      %[[N:.*]] = cir.load {{.*}} %[[N_COPY]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:      cir.call @_ZN9ParamTask12promise_typeC1Ei(%[[PROMISE]], %[[N]])

// LLVM-LABEL: define {{.*}} @_Z10with_parami(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.ParamTask::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

// An over-aligned promise keeps its alignment; coro.id still requests the
// default operator new alignment, as in classic CodeGen.

struct AlignedTask {
  struct alignas(64) promise_type {
    AlignedTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

AlignedTask aligned() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z7alignedv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" align(64) : !cir.ptr<!rec_AlignedTask3A3Apromise_type>
// CIR:         %[[ALIGN:.*]] = cir.const #cir.int<16> : !u32i
// CIR-NEXT:    %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_AlignedTask3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%[[ALIGN]], %[[PROMISE_VOID]], %{{.*}}, %{{.*}})

// LLVM-LABEL: define {{.*}} @_Z7alignedv(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.AlignedTask::promise_type", align 64
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

// Member function and lambda coroutines.

struct S {
  Task member();
};

Task S::member() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_ZN1S6memberEv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})

// LLVM-LABEL: define {{.*}} @_ZN1S6memberEv(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)

void use_lambda() {
  auto l = []() -> Task { co_return; };
  l();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_ZZ10use_lambdavENK3$_0clEv(
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Task3A3Apromise_type>
// CIR:         %[[PROMISE_VOID:.*]] = cir.cast bitcast %[[PROMISE]] : !cir.ptr<!rec_Task3A3Apromise_type> -> !cir.ptr<!void>
// CIR-NEXT:    %{{.*}} = cir.coro.intrinsic.id(%{{.*}}, %[[PROMISE_VOID]], %{{.*}}, %{{.*}})

// LLVM-LABEL: define {{.*}} @"_ZZ10use_lambdavENK3$_0clEv"(
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         call token @llvm.coro.id(i32 16, ptr %[[PROMISE]], ptr null, ptr null)
