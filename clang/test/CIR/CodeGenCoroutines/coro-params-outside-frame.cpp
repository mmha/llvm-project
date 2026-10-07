// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Like classic CodeGen, CIR keeps the original parameters of a coroutine out of
// the coroutine frame: only the ramp function uses them, to initialize the
// parameter copies that the coroutine body uses. The copies stay in the frame.
// A parameter that is passed indirectly has no alloca to mark.

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

struct Pair {
  int a, b;
};

struct NonTrivial {
  NonTrivial(const NonTrivial &);
  ~NonTrivial();
};

Task params(int n, Pair p, const NonTrivial &r, NonTrivial nt) { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z6paramsi4PairRK10NonTrivialS0_(
// CIR:         cir.alloca "n" align(4) init coro_outside_frame : !cir.ptr<!s32i>
// CIR-NEXT:    cir.alloca "p" align(4) init coro_outside_frame : !cir.ptr<!rec_Pair>
// CIR-NEXT:    cir.alloca "r" align(8) init const coro_outside_frame : !cir.ptr<!cir.ptr<!rec_NonTrivial>>
// CIR-NOT:     coro_outside_frame
// CIR:         cir.alloca "n" align(4) init : !cir.ptr<!s32i>
// CIR-NEXT:    cir.alloca "p" align(4) init : !cir.ptr<!rec_Pair>
// CIR-NEXT:    cir.alloca "r" align(8) init const : !cir.ptr<!cir.ptr<!rec_NonTrivial>>
// CIR-NEXT:    cir.alloca "nt" align(1) init : !cir.ptr<!rec_NonTrivial>
// CIR-NOT:     coro_outside_frame
// CIR:         cir.coroutine initialSuspend : {

// LLVM-LABEL: define dso_local void @_Z6paramsi4PairRK10NonTrivialS0_(
// LLVM:         alloca i32, align 4, !coro.outside.frame ![[EMPTY:[0-9]+]]
// LLVM-NEXT:    alloca %struct.Pair, align 4, !coro.outside.frame ![[EMPTY]]
// LLVM-NEXT:    alloca ptr, align 8, !coro.outside.frame ![[EMPTY]]
// LLVM-NOT:     !coro.outside.frame
// LLVM:         call token @llvm.coro.id(
// LLVM:       ![[EMPTY]] = !{}

// OGCG-LABEL: define dso_local void @_Z6paramsi4PairRK10NonTrivialS0_(
// OGCG:         %p = alloca %struct.Pair, align 4, !coro.outside.frame ![[EMPTY:[0-9]+]]
// OGCG-NEXT:    %n.addr = alloca i32, align 4, !coro.outside.frame ![[EMPTY]]
// OGCG-NEXT:    %r.addr = alloca ptr, align 8, !coro.outside.frame ![[EMPTY]]
// OGCG-NOT:     !coro.outside.frame
// OGCG:         call token @llvm.coro.id(
// OGCG:       ![[EMPTY]] = !{}
