// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// FIXME: There is no FlattenCFG or LLVM lowering for cir.coroutine yet, so
// there is no LLVM-through-CIR run.

// A co_await or co_yield expression is a glvalue when await_resume() returns a
// reference. The resume region of the cir.await stores the address that
// await_resume() returns in a temporary, which is reloaded after the cir.await.

#include "Inputs/coroutine.h"

struct S {
  int a;
};

struct RefAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  int &await_resume() noexcept;
};

struct RRefAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  int &&await_resume() noexcept;
};

struct SAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  S &await_resume() noexcept;
};

struct DtorAw {
  ~DtorAw();
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  int &await_resume() noexcept;
};

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    RefAw yield_value(int &&) noexcept;
    RefAw yield_value(int &) noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

void bump(int &);

Task bind_lvalue_reference() {
  int &r = co_await RefAw{};
  r = 5;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z21bind_lvalue_referencev(
// CIR:         %[[R:.*]] = cir.alloca "r" align(8) init const : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR-NEXT:    cir.store align(8) %[[ADDR]], %[[R]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR-NEXT:    %[[FIVE:.*]] = cir.const #cir.int<5> : !s32i
// CIR-NEXT:    %[[R_VAL:.*]] = cir.load %[[R]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR-NEXT:    cir.store align(4) %[[FIVE]], %[[R_VAL]] : !s32i, !cir.ptr<!s32i>

// OGCG-LABEL: define {{.*}} void @_Z21bind_lvalue_referencev(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN5RefAw12await_resumeEv(
// OGCG-NEXT:    store ptr %[[RES]], ptr %[[TMP:.*]], align 8
// OGCG:         %[[ADDR:.*]] = load ptr, ptr %[[TMP]], align 8
// OGCG-NEXT:    store ptr %[[ADDR]], ptr %[[R:.*]], align 8
// OGCG-NEXT:    %[[R_VAL:.*]] = load ptr, ptr %[[R]], align 8
// OGCG-NEXT:    store i32 5, ptr %[[R_VAL]], align 4

Task bind_rvalue_reference() {
  int &&r = co_await RRefAw{};
  r = 6;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z21bind_rvalue_referencev(
// CIR:         %[[R:.*]] = cir.alloca "r" align(8) init const : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN6RRefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:    cir.store align(8) %[[ADDR]], %[[R]]
// CIR-NEXT:    %[[SIX:.*]] = cir.const #cir.int<6> : !s32i
// CIR-NEXT:    %[[R_VAL:.*]] = cir.load %[[R]]
// CIR-NEXT:    cir.store align(4) %[[SIX]], %[[R_VAL]] : !s32i, !cir.ptr<!s32i>

// OGCG-LABEL: define {{.*}} void @_Z21bind_rvalue_referencev(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN6RRefAw12await_resumeEv(
// OGCG-NEXT:    store ptr %[[RES]], ptr %[[TMP:.*]], align 8
// OGCG:         %[[ADDR:.*]] = load ptr, ptr %[[TMP]], align 8
// OGCG-NEXT:    store ptr %[[ADDR]], ptr %[[R:.*]], align 8
// OGCG-NEXT:    %[[R_VAL:.*]] = load ptr, ptr %[[R]], align 8
// OGCG-NEXT:    store i32 6, ptr %[[R_VAL]], align 4

Task assign_to_co_await() {
  (co_await RefAw{}) = 7;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z18assign_to_co_awaitv(
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[SEVEN:.*]] = cir.const #cir.int<7> : !s32i
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:    cir.store align(4) %[[SEVEN]], %[[ADDR]] : !s32i, !cir.ptr<!s32i>

// OGCG-LABEL: define {{.*}} void @_Z18assign_to_co_awaitv(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN5RefAw12await_resumeEv(
// OGCG-NEXT:    store ptr %[[RES]], ptr %{{.*}}, align 8
// OGCG-NEXT:    store i32 7, ptr %[[RES]], align 4

Task pass_by_reference() {
  bump(co_await RefAw{});
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17pass_by_referencev(
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:    cir.call @_Z4bumpRi(%[[ADDR]])

// OGCG-LABEL: define {{.*}} void @_Z17pass_by_referencev(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN5RefAw12await_resumeEv(
// OGCG-NEXT:    call void @_Z4bumpRi(ptr {{.*}} %[[RES]])

// A discarded glvalue co_await is emitted as an lvalue too.

Task discarded() {
  co_await RefAw{};
  (void)co_await RefAw{};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z9discardedv(
// CIR:         %[[SLOT1:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[SLOT2:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES1:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES1]], %[[SLOT1]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES2:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES2]], %[[SLOT2]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)

// OGCG-LABEL: define {{.*}} void @_Z9discardedv(
// OGCG:       await.ready:
// OGCG-NEXT:    call {{.*}} ptr @_ZN5RefAw12await_resumeEv(
// OGCG:       await{{.*}}.ready:
// OGCG-NEXT:    call {{.*}} ptr @_ZN5RefAw12await_resumeEv(

Task member_of_co_await() {
  (co_await SAw{}).a = 8;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z18member_of_co_awaitv(
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!rec_S>>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN3SAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]] : !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!rec_S>>
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]] : !cir.ptr<!cir.ptr<!rec_S>>, !cir.ptr<!rec_S>
// CIR-NEXT:    %[[A:.*]] = cir.get_member %[[ADDR]][0] {name = "a"} : !cir.ptr<!rec_S> -> !cir.ptr<!s32i>
// CIR-NEXT:    cir.store align(4) %{{.*}}, %[[A]] : !s32i, !cir.ptr<!s32i>

// OGCG-LABEL: define {{.*}} void @_Z18member_of_co_awaitv(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN3SAw12await_resumeEv(
// OGCG-NEXT:    %[[A:.*]] = getelementptr inbounds nuw %struct.S, ptr %[[RES]], i32 0, i32 0
// OGCG:         store i32 8, ptr %[[A]], align 4

// The awaiter is destroyed at the end of the full-expression, after the
// address of the result has been reloaded.

Task awaiter_with_dtor() {
  int &r = co_await DtorAw{};
  r = 9;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17awaiter_with_dtorv(
// CIR:         %[[R:.*]] = cir.alloca "r" align(8) init const : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[AW:.*]] = cir.alloca "ref.tmp{{.*}}" align(1) : !cir.ptr<!rec_DtorAw>
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[SPILL:.*]] = cir.alloca "tmp.exprcleanup" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.cleanup.scope {
// CIR-NEXT:      cir.await(user, ready : {
// CIR:           }, resume : {
// CIR-NEXT:        %[[RES:.*]] = cir.call @_ZN6DtorAw12await_resumeEv(%[[AW]])
// CIR-NEXT:        cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:        cir.yield
// CIR-NEXT:      },)
// CIR-NEXT:      %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:      cir.store align(8) %[[ADDR]], %[[SPILL]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      cir.call @_ZN6DtorAwD1Ev(%[[AW]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }
// CIR-NEXT:    %[[ADDR2:.*]] = cir.load align(8) %[[SPILL]]
// CIR-NEXT:    cir.store align(8) %[[ADDR2]], %[[R]]

// OGCG-LABEL: define {{.*}} void @_Z17awaiter_with_dtorv(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN6DtorAw12await_resumeEv(
// OGCG-NEXT:    store ptr %[[RES]], ptr %[[TMP:.*]], align 8
// OGCG:         call void @_ZN6DtorAwD1Ev(
// OGCG:         %[[ADDR:.*]] = load ptr, ptr %[[TMP]], align 8
// OGCG-NEXT:    store ptr %[[ADDR]], ptr %{{.*}}, align 8

Task bind_co_yield() {
  int &y = co_yield 10;
  y = 11;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z13bind_co_yieldv(
// CIR:         %[[Y:.*]] = cir.alloca "y" align(8) init const : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.call @_ZN4Task12promise_type11yield_valueEOi(
// CIR:         cir.await(yield, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:    cir.store align(8) %[[ADDR]], %[[Y]]

// OGCG-LABEL: define {{.*}} void @_Z13bind_co_yieldv(
// OGCG:       yield.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN5RefAw12await_resumeEv(
// OGCG-NEXT:    store ptr %[[RES]], ptr %[[TMP:.*]], align 8
// OGCG:         %[[ADDR:.*]] = load ptr, ptr %[[TMP]], align 8
// OGCG-NEXT:    store ptr %[[ADDR]], ptr %{{.*}}, align 8

// The operand of co_yield binds to the reference parameter of yield_value().

Task co_yield_co_await() {
  co_yield co_await RefAw{};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17co_yield_co_awaitv(
// CIR:         %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      %[[RES:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:      cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:    cir.call @_ZN4Task12promise_type11yield_valueERi(%{{.*}}, %[[ADDR]])
// CIR:         cir.await(yield, ready : {

// OGCG-LABEL: define {{.*}} void @_Z17co_yield_co_awaitv(
// OGCG:       await.ready:
// OGCG-NEXT:    %[[RES:.*]] = call {{.*}} ptr @_ZN5RefAw12await_resumeEv(
// OGCG-NEXT:    call void @_ZN4Task12promise_type11yield_valueERi(ptr {{.*}}, ptr {{.*}} %[[RES]])

// The temporary is in the scope of the co_await.

Task in_nested_scope(bool b) {
  if (b) {
    int &r = co_await RefAw{};
    r = 12;
  }
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z15in_nested_scopeb(
// CIR:         }, body : {
// CIR:         cir.if %{{.*}} {
// CIR-NEXT:      %[[R:.*]] = cir.alloca "r" align(8) init const : !cir.ptr<!cir.ptr<!s32i>>
// CIR:           %[[SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR-NEXT:      cir.await(user, ready : {
// CIR:           }, resume : {
// CIR-NEXT:        %[[RES:.*]] = cir.call @_ZN5RefAw12await_resumeEv(
// CIR-NEXT:        cir.store align(8) %[[RES]], %[[SLOT]]
// CIR-NEXT:        cir.yield
// CIR-NEXT:      },)
// CIR-NEXT:      %[[ADDR:.*]] = cir.load align(8) %[[SLOT]]
// CIR-NEXT:      cir.store align(8) %[[ADDR]], %[[R]]

// The initial and final suspend expressions are glvalues when await_resume()
// returns a reference. With exceptions, the initial await_resume() call is in
// a try/catch and its result is not used: the initial suspend expression then
// yields an lvalue on a null placeholder address, which is never dereferenced.

struct InitRefAw {
  ~InitRefAw();
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  int &await_resume();
};

struct FinalRefAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  int &await_resume() noexcept;
};

struct RefSuspendTask {
  struct promise_type {
    RefSuspendTask get_return_object() noexcept;
    InitRefAw initial_suspend() noexcept;
    FinalRefAw final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

RefSuspendTask initial_and_final_suspend() {
  co_return;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z25initial_and_final_suspendv(
// CIR-NOEH:    %[[INIT_SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR-EH:      %[[RESUME_EH:.*]] = cir.alloca "resume.eh" align(1) : !cir.ptr<!cir.bool>
// CIR:         %[[FINAL_SLOT:.*]] = cir.alloca "__coawait_resume_lval" align(8) : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         cir.coroutine initialSuspend : {
// CIR:           cir.await(init, ready : {
// CIR:           }, resume : {
// CIR-NOEH-NEXT:   %[[RES:.*]] = cir.call @_ZN9InitRefAw12await_resumeEv(
// CIR-NOEH-NEXT:   cir.store align(8) %[[RES]], %[[INIT_SLOT]]
// CIR-EH:            cir.try {
// CIR-EH-NEXT:         cir.call @_ZN9InitRefAw12await_resumeEv(
// CIR-EH-NEXT:         %[[TRUE:.*]] = cir.const #true
// CIR-EH-NEXT:         cir.store %[[TRUE]], %[[RESUME_EH]]
// CIR-EH:          },)
// CIR-EH-NEXT:     %[[PLACEHOLDER:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CIR-EH-NEXT:     cir.store align(8) %[[PLACEHOLDER]], %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR:           cir.call @_ZN9InitRefAwD1Ev(
// CIR:         }, finalSuspend : {
// CIR:           cir.await(final, ready : {
// CIR:           }, resume : {
// CIR-NEXT:        %[[RES:.*]] = cir.call @_ZN10FinalRefAw12await_resumeEv(
// CIR-NEXT:        cir.store align(8) %[[RES]], %[[FINAL_SLOT]]
// CIR-NEXT:        cir.yield
// CIR-NEXT:      },)
// CIR-NEXT:      %{{.*}} = cir.load align(8) %[[FINAL_SLOT]]

// OGCG-LABEL: define {{.*}} @_Z25initial_and_final_suspendv(
// OGCG:       init.ready:
// OGCG-NEXT:    call {{.*}} ptr @_ZN9InitRefAw12await_resumeEv(
// OGCG:       final.ready:
// OGCG-NEXT:    call {{.*}} ptr @_ZN10FinalRefAw12await_resumeEv(
