// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: cir-opt -cir-flatten-cfg %t.cir -o %t-flat.cir
// RUN: FileCheck --input-file=%t-flat.cir %s -check-prefix=CIR-FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir-eh.ll
// RUN: FileCheck --input-file=%t-cir-eh.ll %s -check-prefix=LLVM-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG --implicit-check-not=_Z4deadv
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -disable-llvm-passes %s -o %t-eh.ll
// RUN: FileCheck --input-file=%t-eh.ll %s -check-prefix=OGCG --implicit-check-not=_Z4deadv

// Statements after a co_return that no label makes reachable are dead. As
// after a return, CIRGen emits them into a new block without predecessors,
// which is kept through CFG flattening and the lowering to LLVM IR. Classic
// codegen does not emit them at all.

#include "Inputs/coroutine.h"

struct S {
  ~S();
};

void dead();
void live();

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};

struct IntTask {
  struct promise_type {
    IntTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_value(int);
    void unhandled_exception();
  };
};

IntTask co_return_value(int v) {
  co_return v + 1;
  dead();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z15co_return_valuei(
// CIR:         }, body : {
// CIR-EH:        cir.try {
// CIR:             cir.call @_ZN7IntTask12promise_type12return_valueEi(
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb1:  // no predecessors
// CIR-NEXT:        cir.call @_Z4deadv()
// CIR-NEXT:        cir.yield
// CIR-EH-NEXT:   } catch all
// CIR:         }, finalSuspend : {

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z15co_return_valuei(
// CIR-FLAT:         cir.call @_ZN7IntTask12promise_type12return_valueEi(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.call @_Z4deadv()

// LLVM-LABEL: define {{.*}} @_Z15co_return_valuei(
// LLVM:         call void @_ZN7IntTask12promise_type12return_valueEi(
// LLVM-NEXT:    br label %[[FINAL:[0-9]+]]
// LLVM:       {{[0-9]+}}:{{ *}}; No predecessors!
// LLVM-NEXT:    call void @_Z4deadv()
// LLVM-NEXT:    br label %[[FINAL]]
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN7IntTask12promise_type13final_suspendEv(

// OGCG-LABEL: define {{.*}} @_Z15co_return_valuei(
// OGCG:         {{call|invoke}} void @_ZN7IntTask12promise_type12return_valueEi(

// A co_return at the top level of the body still suppresses the implicit
// co_return when unreachable statements follow it.

Task co_return_void() {
  co_return;
  S s;
  dead();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z14co_return_voidv(
// CIR:         }, body : {
// CIR-EH:        cir.try {
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb1:  // no predecessors
// CIR-NEXT:        cir.cleanup.scope {
// CIR-NEXT:          cir.call @_Z4deadv()
// CIR-NEXT:          cir.yield
// CIR-NOEH-NEXT:   } cleanup normal {
// CIR-EH-NEXT:     } cleanup all {
// CIR-NEXT:          cir.call @_ZN1SD1Ev(
// CIR-NEXT:          cir.yield
// CIR-NEXT:        }
// CIR-NEXT:        cir.yield
// CIR-EH-NEXT:   } catch all
// CIR-NOT:       @_ZN4Task12promise_type11return_voidEv
// CIR:         }, finalSuspend : {

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z14co_return_voidv(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT:         cir.call @_Z4deadv()
// CIR-FLAT-NOT:     @_ZN4Task12promise_type11return_voidEv
// CIR-FLAT:         cir.label "coro.final"

// LLVM-LABEL: define {{.*}} @_Z14co_return_voidv(
// LLVM:         call void @_ZN4Task12promise_type11return_voidEv(
// LLVM-NEXT:    br label %[[FINAL:[0-9]+]]
// LLVM:       {{[0-9]+}}:{{ *}}; No predecessors!
// LLVM:         call void @_Z4deadv()
// LLVM:         call void @_ZN1SD1Ev(
// LLVM-NOT:     @_ZN4Task12promise_type11return_voidEv
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN4Task12promise_type13final_suspendEv(

// OGCG-LABEL: define {{.*}} @_Z14co_return_voidv(
// OGCG:         {{call|invoke}} void @_ZN4Task12promise_type11return_voidEv(
// OGCG-NOT:     @_ZN1SD1Ev
// OGCG:       coro.final:

// Without unreachable statements, the new block stays empty. It is erased at
// the end of the body region, but, as after a return, a cir.try terminates it
// with a cir.yield.

Task co_return_at_end() {
  co_await std::suspend_always{};
  co_return;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z16co_return_at_endv(
// CIR:         }, body : {
// CIR:             cir.await(user,
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:        cir.co_return
// CIR-NOEH-NEXT: }, finalSuspend : {
// CIR-EH-NEXT:   ^bb1:  // no predecessors
// CIR-EH-NEXT:     cir.yield
// CIR-EH-NEXT:   } catch all

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z16co_return_at_endv(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.label "coro.final"

Task co_return_in_block() {
  {
    S s;
    co_return;
    dead();
  }
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z18co_return_in_blockv(
// CIR:         }, body : {
// CIR:             cir.scope {
// CIR:               cir.cleanup.scope {
// CIR-NEXT:            cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:            cir.co_return
// CIR-NEXT:          ^bb1:  // no predecessors
// CIR-NEXT:            cir.call @_Z4deadv()
// CIR-NEXT:            cir.yield
// CIR-NOEH-NEXT:     } cleanup normal {
// CIR-EH-NEXT:       } cleanup all {
// CIR-NEXT:            cir.call @_ZN1SD1Ev(
// CIR-NEXT:            cir.yield
// CIR-NEXT:          }
// CIR-NEXT:        }

// The co_return leaves the scope through the cleanup of s, on its way to the
// final suspend point.
//
// CIR-FLAT-LABEL: cir.func {{.*}} @_Z18co_return_in_blockv(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    %[[ONE:.*]] = cir.const #cir.int<1> : !s32i
// CIR-FLAT-NEXT:    cir.store %[[ONE]], %[[SLOT:.*]] : !s32i, !cir.ptr<!s32i>
// CIR-FLAT-NEXT:    cir.br ^[[CLEANUP:bb[0-9]+]]
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.call @_Z4deadv()
// CIR-FLAT:       ^[[CLEANUP]]:
// CIR-FLAT-NEXT:    cir.call @_ZN1SD1Ev(
// CIR-FLAT:         cir.switch.flat %{{.*}} : !s32i, ^{{bb[0-9]+}} [
// CIR-FLAT-NEXT:      0: ^{{bb[0-9]+}},
// CIR-FLAT-NEXT:      1: ^[[TO_FINAL:bb[0-9]+]]
// CIR-FLAT-NEXT:    ]
// CIR-FLAT:       ^[[TO_FINAL]]:
// CIR-FLAT-NEXT:    cir.goto "coro.final"

// LLVM-LABEL: define {{.*}} @_Z18co_return_in_blockv(
// LLVM:         call void @_ZN4Task12promise_type11return_voidEv(
// LLVM:       {{[0-9]+}}:{{ *}}; No predecessors!
// LLVM-NEXT:    call void @_Z4deadv()
// LLVM:         call void @_ZN1SD1Ev(

// OGCG-LABEL: define {{.*}} @_Z18co_return_in_blockv(
// OGCG:         {{call|invoke}} void @_ZN4Task12promise_type11return_voidEv(
// OGCG:         call void @_ZN1SD1Ev(

Task co_return_in_if(bool b) {
  if (b) {
    co_return;
    dead();
  } else {
    co_return;
    dead();
  }
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z15co_return_in_ifb(
// CIR:         }, body : {
// CIR:             cir.if %{{.*}} {
// CIR-NEXT:          cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:          cir.co_return
// CIR-NEXT:        ^bb1:  // no predecessors
// CIR-NEXT:          cir.call @_Z4deadv()
// CIR-NEXT:          cir.yield
// CIR-NEXT:        } else {
// CIR-NEXT:          cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:          cir.co_return
// CIR-NEXT:        ^bb1:  // no predecessors
// CIR-NEXT:          cir.call @_Z4deadv()
// CIR-NEXT:          cir.yield
// CIR-NEXT:        }

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z15co_return_in_ifb(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.call @_Z4deadv()
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.call @_Z4deadv()

Task co_return_in_loop(bool b) {
  while (b) {
    co_return;
    dead();
  }
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17co_return_in_loopb(
// CIR:         }, body : {
// CIR:             cir.while {
// CIR:             } do {
// CIR-NEXT:          cir.scope {
// CIR-NEXT:            cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:            cir.co_return
// CIR-NEXT:          ^bb1:  // no predecessors
// CIR-NEXT:            cir.call @_Z4deadv()
// CIR-NEXT:            cir.yield
// CIR-NEXT:          }

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z17co_return_in_loopb(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.call @_Z4deadv()

Task co_return_in_switch(int i) {
  switch (i) {
  case 0:
    co_return;
    dead();
  default:
    break;
  }
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z19co_return_in_switchi(
// CIR:         }, body : {
// CIR:             cir.case(equal, [#cir.int<0> : !s32i]) {
// CIR-NEXT:          cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:          cir.co_return
// CIR-NEXT:        ^bb1:  // no predecessors
// CIR-NEXT:          cir.call @_Z4deadv()
// CIR-NEXT:          cir.yield
// CIR-NEXT:        }

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z19co_return_in_switchi(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.call @_Z4deadv()

// An unreachable suspension point is emitted too.

Task dead_co_await() {
  co_return;
  co_await std::suspend_never{};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z13dead_co_awaitv(
// CIR:         }, body : {
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb1:  // no predecessors
// CIR-NEXT:        cir.await(user, ready : {
// CIR-NEXT:          cir.call @_ZNSt13suspend_never11await_readyEv(
// CIR-NOT:       @_ZN4Task12promise_type11return_voidEv
// CIR:         }, finalSuspend : {

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z13dead_co_awaitv(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.br ^[[READY:bb[0-9]+]]
// CIR-FLAT-NEXT:  ^[[READY]]:
// CIR-FLAT-NEXT:    cir.call @_ZNSt13suspend_never11await_readyEv(

// LLVM-LABEL: define {{.*}} @_Z13dead_co_awaitv(
// LLVM:         call void @_ZN4Task12promise_type11return_voidEv(
// LLVM-NEXT:    br label %[[FINAL:[0-9]+]]
// LLVM:       {{[0-9]+}}:{{ *}}; No predecessors!
// LLVM:         call {{.*}} @_ZNSt13suspend_never11await_readyEv(
// LLVM:         call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM:         call void @_ZNSt13suspend_never12await_resumeEv(
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN4Task12promise_type13final_suspendEv(

// OGCG-LABEL: define {{.*}} @_Z13dead_co_awaitv(
// OGCG-NOT:     suspend_never
// OGCG:       coro.final:

// A label after a co_return makes the statements after it reachable through a
// goto, and with them the end of the body. The implicit co_return is then
// emitted, as in classic codegen, which calls return_void() on that path.

Task label_after_co_return(bool b) {
  if (b)
    goto l;
  co_return;
l:
  live();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z21label_after_co_returnb(
// CIR:         }, body : {
// CIR:             cir.goto "l"
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(%[[PROMISE:[0-9]+]])
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb1:  // no predecessors
// CIR-NEXT:        cir.label "l"
// CIR-NEXT:        cir.call @_Z4livev()
// CIR-NEXT:        cir.call @_ZN4Task12promise_type11return_voidEv(%[[PROMISE]])
// CIR-NEXT:        cir.co_return
// CIR-NOEH-NEXT: }, finalSuspend : {
// CIR-EH-NEXT:   ^bb2:  // no predecessors
// CIR-EH-NEXT:     cir.yield
// CIR-EH-NEXT:   } catch all

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z21label_after_co_returnb(
// CIR-FLAT:         cir.goto "l"
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.label "l"
// CIR-FLAT-NEXT:    cir.call @_Z4livev()
// CIR-FLAT-NEXT:    cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.label "coro.final"

// LLVM-LABEL: define {{.*}} @_Z21label_after_co_returnb(
// LLVM:         call void @_ZN4Task12promise_type11return_voidEv(
// LLVM-NEXT:    br label %[[FINAL:[0-9]+]]
// LLVM:         call void @_Z4livev()
// LLVM-NEXT:    call void @_ZN4Task12promise_type11return_voidEv(
// LLVM-NEXT:    br label %[[FINAL]]
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN4Task12promise_type13final_suspendEv(

// OGCG-LABEL: define {{.*}} @_Z21label_after_co_returnb(
// OGCG:       {{^}}l:
// OGCG-NEXT:    {{call|invoke}} void @_Z4livev()
// OGCG:         {{call|invoke}} void @_ZN4Task12promise_type11return_voidEv(
// OGCG:         br label %coro.final

// The same holds for a label in a nested block and for a label at the end of
// the body.

Task label_in_block_after_co_return(bool b) {
  if (b)
    goto l;
  co_return;
  {
  l:
    live();
  }
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z30label_in_block_after_co_returnb(
// CIR:         }, body : {
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(%[[PROMISE:[0-9]+]])
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb1:  // no predecessors
// CIR-NEXT:        cir.scope {
// CIR:               cir.label "l"
// CIR-NEXT:          cir.call @_Z4livev()
// CIR-NEXT:          cir.yield
// CIR-NEXT:        }
// CIR-NEXT:        cir.call @_ZN4Task12promise_type11return_voidEv(%[[PROMISE]])
// CIR-NEXT:        cir.co_return

// OGCG-LABEL: define {{.*}} @_Z30label_in_block_after_co_returnb(
// OGCG:       {{^}}l:
// OGCG-NEXT:    {{call|invoke}} void @_Z4livev()
// OGCG:         {{call|invoke}} void @_ZN4Task12promise_type11return_voidEv(
// OGCG:         br label %coro.final

Task empty_label_after_co_return(bool b) {
  if (b)
    goto l;
  co_return;
l:;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z27empty_label_after_co_returnb(
// CIR:         }, body : {
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(%[[PROMISE:[0-9]+]])
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb1:  // no predecessors
// CIR-NEXT:        cir.label "l"
// CIR-NEXT:        cir.call @_ZN4Task12promise_type11return_voidEv(%[[PROMISE]])
// CIR-NEXT:        cir.co_return

// OGCG-LABEL: define {{.*}} @_Z27empty_label_after_co_returnb(
// OGCG:       {{^}}l:
// OGCG-NEXT:    {{call|invoke}} void @_ZN4Task12promise_type11return_voidEv(
// OGCG:         br label %coro.final

// A co_return after the label makes the end of the body unreachable again.

Task co_return_after_label(bool b) {
  if (b)
    goto l;
  co_return;
l:
  live();
  co_return;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z21co_return_after_labelb(
// CIR:         }, body : {
// CIR:             cir.label "l"
// CIR-NEXT:        cir.call @_Z4livev()
// CIR-NEXT:        cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:        cir.co_return
// CIR-NOT:         @_ZN4Task12promise_type11return_voidEv
// CIR:         }, finalSuspend : {

// A goto, __builtin_unreachable() or __builtin_trap() at the end of the body
// leaves an empty block behind as well. Without a fall-through handler, which
// a promise without return_void() or a co_return at the top level of the body
// rules out, the block is terminated with a cir.yield.

IntTask goto_at_end() {
l:
  co_await std::suspend_always{};
  goto l;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z11goto_at_endv(
// CIR:         }, body : {
// CIR:             cir.label "l"
// CIR:             cir.goto "l"
// CIR-NEXT:      ^bb{{[0-9]+}}:  // no predecessors
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: }, finalSuspend : {
// CIR-EH-NEXT:   } catch all

// The empty block falls through to the final suspend point.
//
// CIR-FLAT-LABEL: cir.func {{.*}} @_Z11goto_at_endv(
// CIR-FLAT:         cir.goto "l"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.br ^[[FINAL:bb[0-9]+]]
// CIR-FLAT-NEXT:  ^[[FINAL]]:
// CIR-FLAT-NEXT:    cir.label "coro.final"

Task goto_after_co_return() {
l:
  co_await std::suspend_always{};
  co_return;
  goto l;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z20goto_after_co_returnv(
// CIR:         }, body : {
// CIR:             cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:        cir.co_return
// CIR-NEXT:      ^bb{{[0-9]+}}:  // no predecessors
// CIR-NEXT:        cir.goto "l"
// CIR-NEXT:      ^bb{{[0-9]+}}:  // no predecessors
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: }, finalSuspend : {
// CIR-EH-NEXT:   } catch all

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z20goto_after_co_returnv(
// CIR-FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-FLAT-NEXT:    cir.goto "coro.final"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.goto "l"
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.br ^[[FINAL:bb[0-9]+]]
// CIR-FLAT-NEXT:  ^[[FINAL]]:
// CIR-FLAT-NEXT:    cir.label "coro.final"

IntTask unreachable_after_switch(int i) {
  switch (i) {
  case 0:
    co_return 1;
  default:
    co_return 2;
  }
  __builtin_unreachable();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z24unreachable_after_switchi(
// CIR:         }, body : {
// CIR:             cir.switch
// CIR:             cir.unreachable
// CIR-NEXT:      ^bb{{[0-9]+}}:  // no predecessors
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: }, finalSuspend : {
// CIR-EH-NEXT:   } catch all

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z24unreachable_after_switchi(
// CIR-FLAT:         cir.unreachable
// CIR-FLAT-NEXT:  ^bb{{[0-9]+}}:  // no predecessors
// CIR-FLAT-NEXT:    cir.br ^[[FINAL:bb[0-9]+]]
// CIR-FLAT-NEXT:  ^[[FINAL]]:
// CIR-FLAT-NEXT:    cir.label "coro.final"

IntTask trap_at_end() {
  co_await std::suspend_always{};
  __builtin_trap();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z11trap_at_endv(
// CIR:         }, body : {
// CIR:             cir.trap
// CIR-NEXT:      ^bb{{[0-9]+}}:  // no predecessors
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: }, finalSuspend : {
// CIR-EH-NEXT:   } catch all

#if __cpp_exceptions
Task co_return_in_try() {
  try {
    co_return;
    dead();
  } catch (...) {
    co_return;
    dead();
  }
}
#endif

// CIR-EH-LABEL: cir.func coroutine {{.*}} @_Z16co_return_in_tryv(
// CIR-EH:         }, body : {
// CIR-EH:           cir.try {
// CIR-EH:             cir.try {
// CIR-EH-NEXT:          cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-EH-NEXT:          cir.co_return
// CIR-EH-NEXT:        ^bb1:  // no predecessors
// CIR-EH-NEXT:          cir.call @_Z4deadv()
// CIR-EH-NEXT:          cir.yield
// CIR-EH-NEXT:        } catch all
// CIR-EH-NEXT:          cir.begin_catch
// CIR-EH-NEXT:          cir.cleanup.scope {
// CIR-EH-NEXT:            cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-EH-NEXT:            cir.co_return
// CIR-EH-NEXT:          ^bb1:  // no predecessors
// CIR-EH-NEXT:            cir.call @_Z4deadv()
// CIR-EH-NEXT:            cir.yield
// CIR-EH-NEXT:          } cleanup all {
// CIR-EH-NEXT:            cir.end_catch

// LLVM-EH-LABEL: define {{.*}} @_Z16co_return_in_tryv(
// LLVM-EH:         invoke void @_ZN4Task12promise_type11return_voidEv(
// LLVM-EH-NEXT:            to label %{{.*}} unwind label %[[LPAD:[0-9]+]]
// LLVM-EH:       {{[0-9]+}}:{{ *}}; No predecessors!
// LLVM-EH-NEXT:    invoke void @_Z4deadv()
// LLVM-EH-NEXT:            to label %{{.*}} unwind label %[[LPAD]]
// LLVM-EH:       [[LPAD]]:
// LLVM-EH-NEXT:    landingpad { ptr, i32 }
// LLVM-EH-NEXT:            catch ptr null
// LLVM-EH:         call ptr @__cxa_begin_catch(
// LLVM-EH:         invoke void @_ZN4Task12promise_type11return_voidEv(
// LLVM-EH:       {{[0-9]+}}:{{ *}}; No predecessors!
// LLVM-EH-NEXT:    invoke void @_Z4deadv()
// LLVM-EH:         call void @__cxa_end_catch()
