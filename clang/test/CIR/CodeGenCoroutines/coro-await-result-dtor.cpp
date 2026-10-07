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
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -disable-llvm-passes %s -o %t-eh.ll
// RUN: FileCheck --input-file=%t-eh.ll %s -check-prefix=OGCG-EH

// The result of await_resume() is constructed in the resume region of the
// cir.await. A temporary result lives until the end of the full-expression, so
// unless something else destroys it, its destructor is pushed after the
// cir.await, and the resume region still ends with its cir.yield.

#include "Inputs/coroutine.h"

struct NT {
  int x;
  ~NT();
};

struct Aw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  NT await_resume();
};

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    Aw yield_value(int) noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

struct H {
  ~H();
};

H make_h();
void next();
void take(NT);
void take2(NT, NT);
void use(const NT &);

Task discarded_co_await() {
  co_await Aw{};
  next();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z18discarded_co_awaitv(
// CIR:         %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[TMP]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT:} cleanup normal {
// CIR-EH-NEXT: } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[TMP]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }
// CIR-NEXT:    cir.call @_Z4nextv()

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z18discarded_co_awaitv(
// CIR-FLAT:         cir.call @_ZN2Aw12await_resumeEv(%[[TMP:[0-9]+]], %{{.*}})
// CIR-FLAT:         cir.call @_ZN2NTD1Ev(%[[TMP]])
// CIR-FLAT-NOT:     cir.call @_ZN2NTD1Ev(
// CIR-FLAT:         cir.call @_Z4nextv()

// LLVM-LABEL: define {{.*}} void @_Z18discarded_co_awaitv(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])
// LLVM-NOT:     call void @_ZN2NTD1Ev(
// LLVM:         call void @_Z4nextv()

// OGCG-LABEL: define {{.*}} void @_Z18discarded_co_awaitv(
// OGCG:       await.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])
// OGCG:         call void @_Z4nextv()

Task discarded_co_yield() {
  co_yield 1;
  next();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z18discarded_co_yieldv(
// CIR:         %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR:         cir.await(yield, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[TMP]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT:} cleanup normal {
// CIR-EH-NEXT: } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[TMP]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }
// CIR-NEXT:    cir.call @_Z4nextv()

// LLVM-LABEL: define {{.*}} void @_Z18discarded_co_yieldv(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])
// LLVM-NOT:     call void @_ZN2NTD1Ev(
// LLVM:         call void @_Z4nextv()

// OGCG-LABEL: define {{.*}} void @_Z18discarded_co_yieldv(
// OGCG:       yield.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])
// OGCG:         call void @_Z4nextv()

// The result is destroyed at the end of the full-expression, after the
// temporaries created after it.

Task discarded_with_later_temporary() {
  (co_await Aw{}, make_h());
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z30discarded_with_later_temporaryv(
// CIR:         %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR:         %[[H:.*]] = cir.alloca "agg.tmp.ensured" align(1) : !cir.ptr<!rec_H>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[TMP]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.call @_Z6make_hv(%[[H]])
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        cir.call @_ZN1HD1Ev(%[[H]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[TMP]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z30discarded_with_later_temporaryv(
// CIR-FLAT:         cir.call @_ZN2Aw12await_resumeEv(%[[TMP:[0-9]+]], %{{.*}})
// CIR-FLAT:         cir.call @_Z6make_hv(%[[H:[0-9]+]])
// CIR-FLAT:         cir.call @_ZN1HD1Ev(%[[H]])
// CIR-FLAT:         cir.call @_ZN2NTD1Ev(%[[TMP]])

// LLVM-LABEL: define {{.*}} void @_Z30discarded_with_later_temporaryv(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// LLVM:         call void @_Z6make_hv(ptr {{.*}} %[[H:.*]])
// LLVM:         call void @_ZN1HD1Ev(ptr {{.*}} %[[H]])
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])

// LLVM-EH-LABEL: define {{.*}} void @_Z30discarded_with_later_temporaryv(
// LLVM-EH:         invoke void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// LLVM-EH:         invoke void @_Z6make_hv(
// LLVM-EH-NEXT:      to label %{{.*}} unwind label %[[LPAD:[0-9]+]]
// LLVM-EH:       [[LPAD]]:
// LLVM-EH-NEXT:    landingpad
// LLVM-EH:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])

// OGCG-LABEL: define {{.*}} void @_Z30discarded_with_later_temporaryv(
// OGCG:       await.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// OGCG-NEXT:    call void @_Z6make_hv(ptr {{.*}} %[[H:.*]])
// OGCG-NEXT:    call void @_ZN1HD1Ev(ptr {{.*}} %[[H]])
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])

// OGCG-EH-LABEL: define {{.*}} void @_Z30discarded_with_later_temporaryv(
// OGCG-EH:       await.ready:
// OGCG-EH-NEXT:    invoke void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// OGCG-EH:         invoke void @_Z6make_hv(
// OGCG-EH-NEXT:      to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG-EH:       [[LPAD]]:
// OGCG-EH-NEXT:    landingpad
// OGCG-EH:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])

// A by-value argument is destroyed by the caller after the call.

Task by_value_argument() {
  take(co_await Aw{});
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17by_value_argumentv(
// CIR:         %[[ARG:.*]] = cir.alloca "agg.tmp{{[0-9]+}}" align(4) : !cir.ptr<!rec_NT>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[ARG]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.call @_Z4take2NT(%[[ARG]])
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT:} cleanup normal {
// CIR-EH-NEXT: } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[ARG]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }

// LLVM-LABEL: define {{.*}} void @_Z17by_value_argumentv(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[ARG:.*]], ptr
// LLVM:         call void @_Z4take2NT(ptr {{.*}} %[[ARG]])
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[ARG]])
// LLVM-NOT:     call void @_ZN2NTD1Ev(

// OGCG-LABEL: define {{.*}} void @_Z17by_value_argumentv(
// OGCG:       await.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[ARG:.*]], ptr
// OGCG-NEXT:    call void @_Z4take2NT(ptr {{.*}} %[[ARG]])
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[ARG]])

// A variable initialized by the result destroys it at the end of its scope.

Task bound_to_variable() {
  NT v = co_await Aw{};
  next();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17bound_to_variablev(
// CIR:         %[[V:.*]] = cir.alloca "v" align(4) init : !cir.ptr<!rec_NT>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[V]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.call @_Z4nextv()
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT:} cleanup normal {
// CIR-EH-NEXT: } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[V]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }

// LLVM-LABEL: define {{.*}} void @_Z17bound_to_variablev(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[V:.*]], ptr
// LLVM:         call void @_Z4nextv()
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[V]])
// LLVM-NOT:     call void @_ZN2NTD1Ev(

// OGCG-LABEL: define {{.*}} void @_Z17bound_to_variablev(
// OGCG:       await.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[V:.*]], ptr
// OGCG:         call void @_Z4nextv()
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[V]])

// A materialized temporary is destroyed at the end of the full-expression.

Task materialized() {
  use(co_await Aw{});
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z12materializedv(
// CIR:         %[[REF:.*]] = cir.alloca "ref.tmp{{[0-9]+}}" align(4) : !cir.ptr<!rec_NT>
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[REF]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.call @_Z3useRK2NT(%[[REF]])
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT:} cleanup normal {
// CIR-EH-NEXT: } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[REF]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }

// LLVM-LABEL: define {{.*}} void @_Z12materializedv(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[REF:.*]], ptr
// LLVM:         call void @_Z3useRK2NT(ptr {{.*}} %[[REF]])
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[REF]])
// LLVM-NOT:     call void @_ZN2NTD1Ev(

// OGCG-LABEL: define {{.*}} void @_Z12materializedv(
// OGCG:       await.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[REF:.*]], ptr
// OGCG-NEXT:    call void @_Z3useRK2NT(ptr {{.*}} %[[REF]])
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[REF]])

// The first result lives across the second suspend point, so its cleanup
// encloses the second cir.await: if the coroutine is destroyed while suspended
// there, the first result is destroyed.

Task two_co_awaits() {
  take2(co_await Aw{}, co_await Aw{});
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z13two_co_awaitsv(
// CIR:         cir.await(user, ready : {
// CIR:         }, resume : {
// CIR-NEXT:      cir.call @_ZN2Aw12await_resumeEv(%[[A1:.*]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    },)
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.await(user, ready : {
// CIR:           }, resume : {
// CIR-NEXT:        cir.call @_ZN2Aw12await_resumeEv(%[[A2:.*]], %{{.*}})
// CIR-NEXT:        cir.yield
// CIR-NEXT:      },)
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.call @_Z5take22NTS_(%[[A1]], %[[A2]])
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT:   } cleanup normal {
// CIR-EH-NEXT:     } cleanup all {
// CIR-NEXT:        cir.call @_ZN2NTD1Ev(%[[A2]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      cir.call @_ZN2NTD1Ev(%[[A1]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z13two_co_awaitsv(
// CIR-FLAT:         cir.call @_ZN2Aw12await_resumeEv(%[[A1:[0-9]+]], %{{.*}})
// CIR-FLAT:         cir.call @_ZN2Aw12await_resumeEv(%[[A2:[0-9]+]], %{{.*}})
// CIR-FLAT:         cir.call @_Z5take22NTS_(%[[A1]], %[[A2]])
// CIR-FLAT:         cir.call @_ZN2NTD1Ev(%[[A2]])
// CIR-FLAT:         cir.call @_ZN2NTD1Ev(%[[A1]])

// Destroying the coroutine at the second suspend point destroys the first
// result only.
//
// LLVM-LABEL: define {{.*}} void @_Z13two_co_awaitsv(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[A1:[0-9]+]], ptr
// LLVM:         call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z13two_co_awaitsv.__await_suspend_wrapper__await2)
// LLVM-NEXT:    %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[A1_CLEANUP:[0-9]+]]
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[A2:[0-9]+]], ptr
// LLVM:         call void @_Z5take22NTS_(ptr {{.*}} %[[A1]], ptr {{.*}} %[[A2]])
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[A2]])
// LLVM:         store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[A1_CLEANUP]]
// LLVM:       [[A1_CLEANUP]]:
// LLVM-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[A1]])

// OGCG-LABEL: define {{.*}} void @_Z13two_co_awaitsv(
// OGCG:       await2.cleanup:
// OGCG-NEXT:    br label %[[CLEANUP:.*]]
// OGCG:       await2.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[A2:.*]], ptr
// OGCG-NEXT:    call void @_Z5take22NTS_(ptr {{.*}} %[[A1:agg.tmp[0-9]*]], ptr {{.*}} %[[A2]])
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[A2]])
// OGCG:       [[CLEANUP]]:
// OGCG:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[A1]])

// A result in a conditional branch is destroyed at the end of the
// full-expression only if the branch was taken. The flag that records this is
// set after the cir.await.

Task conditional(bool b) {
  b ? (void)co_await Aw{} : (void)0;
  next();
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z11conditionalb(
// CIR:         %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR:         cir.cleanup.scope {
// CIR-NEXT:      %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE:[0-9]+]] :
// CIR-NEXT:      cir.ternary
// CIR:             cir.await(user, ready : {
// CIR:             }, resume : {
// CIR-NEXT:          cir.call @_ZN2Aw12await_resumeEv(%[[TMP]], %{{.*}})
// CIR-NEXT:          cir.yield
// CIR-NEXT:        },)
// CIR-NEXT:        %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:        cir.store %[[TRUE]], %[[ACTIVE]]
// CIR-NOEH:    } cleanup normal {
// CIR-EH:      } cleanup all {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN2NTD1Ev(%[[TMP]])
// CIR:         cir.call @_Z4nextv()

// LLVM-LABEL: define {{.*}} void @_Z11conditionalb(
// LLVM:         call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:[0-9]+]], ptr
// LLVM-NEXT:    br label %[[SET:[0-9]+]]
// LLVM:       [[SET]]:
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])
// LLVM:         call void @_Z4nextv()

// OGCG-LABEL: define {{.*}} void @_Z11conditionalb(
// OGCG:       await.ready:
// OGCG-NEXT:    call void @_ZN2Aw12await_resumeEv(ptr {{.*}} %[[TMP:.*]], ptr
// OGCG-NEXT:    store i1 true, ptr %[[ACTIVE:.*]], align 1
// OGCG:         %[[IS_ACTIVE:.*]] = load i1, ptr %[[ACTIVE]]
// OGCG-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:.*]], label
// OGCG:       [[DTOR]]:
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[TMP]])
// OGCG:         call void @_Z4nextv()

// The results of the initial and final suspend points are discarded too. With
// exceptions, an initial await_resume() call that may throw, or whose result's
// destructor may throw, is called in a try/catch that passes its exceptions to
// unhandled_exception(). The result is destroyed in the try block, at the end
// of the full-expression, before the flag that lets the body run is set, so an
// exception from the destructor skips the body as well.

struct InitAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  NT await_resume();
};

struct FinalAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  NT await_resume() noexcept;
};

struct InitFinalTask {
  struct promise_type {
    InitFinalTask get_return_object() noexcept;
    InitAw initial_suspend() noexcept;
    FinalAw final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

InitFinalTask initial_and_final_suspend() {
  next();
  co_return;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z25initial_and_final_suspendv(
// CIR-NOEH:    %[[INIT:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR-EH:      %[[RESUME_EH:.*]] = cir.alloca "resume.eh"
// CIR:         %[[FINAL:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR:         cir.coroutine initialSuspend : {
// CIR:           cir.await(init, ready : {
// CIR:           }, resume : {
// CIR-NOEH-NEXT:   cir.call @_ZN6InitAw12await_resumeEv(%[[INIT]], %{{.*}})
// CIR-NOEH-NEXT:   cir.yield
// CIR-NOEH-NEXT: },)
// CIR-NOEH-NEXT: cir.cleanup.scope {
// CIR-NOEH-NEXT:   cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-NOEH-NEXT:   cir.call @_ZN2NTD1Ev(%[[INIT]])
// CIR-NOEH-NEXT:   cir.yield
// CIR-NOEH-NEXT: }
// CIR-EH:          cir.scope {
// CIR-EH-NEXT:       %[[INIT:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR-EH-NEXT:       cir.try {
// CIR-EH-NEXT:         cir.call @_ZN6InitAw12await_resumeEv(%[[INIT]], %{{.*}})
// CIR-EH-NEXT:         cir.cleanup.scope {
// CIR-EH-NEXT:           cir.yield
// CIR-EH-NEXT:         } cleanup all {
// CIR-EH-NEXT:           cir.call @_ZN2NTD1Ev(%[[INIT]])
// CIR-EH-NEXT:           cir.yield
// CIR-EH-NEXT:         }
// CIR-EH-NEXT:         %[[TRUE:.*]] = cir.const #true
// CIR-EH-NEXT:         cir.store %[[TRUE]], %[[RESUME_EH]]
// CIR-EH-NEXT:         cir.yield
// CIR-EH-NEXT:       } catch all
// CIR:         }, body : {
// CIR:         }, finalSuspend : {
// CIR:           cir.await(final, ready : {
// CIR:           }, resume : {
// CIR-NEXT:        cir.call @_ZN7FinalAw12await_resumeEv(%[[FINAL]], %{{.*}})
// CIR-NEXT:        cir.yield
// CIR-NEXT:      },)
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        cir.call @_ZN2NTD1Ev(%[[FINAL]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }, destroy : {

// LLVM-LABEL: define {{.*}} void @_Z25initial_and_final_suspendv(
// LLVM:         call void @_ZN6InitAw12await_resumeEv(ptr {{.*}} %[[INIT:.*]], ptr
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[INIT]])
// LLVM:         call void @_Z4nextv()
// LLVM:         call void @_ZN7FinalAw12await_resumeEv(ptr {{.*}} %[[FINAL:.*]], ptr
// LLVM:         call void @_ZN2NTD1Ev(ptr {{.*}} %[[FINAL]])

// OGCG-LABEL: define {{.*}} void @_Z25initial_and_final_suspendv(
// OGCG:       init.ready:
// OGCG-NEXT:    call void @_ZN6InitAw12await_resumeEv(ptr {{.*}} %[[INIT:.*]], ptr
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[INIT]])
// OGCG:       final.ready:
// OGCG-NEXT:    call void @_ZN7FinalAw12await_resumeEv(ptr {{.*}} %[[FINAL:.*]], ptr
// OGCG-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[FINAL]])

// OGCG-EH-LABEL: define {{.*}} void @_Z25initial_and_final_suspendv(
// OGCG-EH:       init.ready:
// OGCG-EH-NEXT:    store i1 true, ptr %[[RESUME_EH:.*]], align 1
// OGCG-EH-NEXT:    invoke void @_ZN6InitAw12await_resumeEv(ptr {{.*}} %[[INIT:.*]], ptr
// OGCG-EH-NEXT:      to label %[[CONT:.*]] unwind label
// OGCG-EH:       [[CONT]]:
// OGCG-EH-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[INIT]])
// OGCG-EH-NEXT:    store i1 false, ptr %[[RESUME_EH]], align 1

// The try/catch is only needed if the resume expression can throw: here
// neither await_resume() nor the result's destructor can.

struct NoexceptInitAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  NT await_resume() noexcept;
};

struct NoexceptInitTask {
  struct promise_type {
    NoexceptInitTask get_return_object() noexcept;
    NoexceptInitAw initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

NoexceptInitTask noexcept_initial_resume() {
  next();
  co_return;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z23noexcept_initial_resumev(
// CIR-NOT:       resume.eh
// CIR:           %[[INIT:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_NT>
// CIR-NOT:       resume.eh
// CIR:           cir.await(init, ready : {
// CIR:           }, resume : {
// CIR-NEXT:        cir.call @_ZN14NoexceptInitAw12await_resumeEv(%[[INIT]], %{{.*}})
// CIR-NEXT:        cir.yield
// CIR-NEXT:      },)
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        cir.call @_ZN2NTD1Ev(%[[INIT]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }, body : {
// CIR-NOT:       resume.eh
// CIR:         }, finalSuspend : {

// OGCG-EH-LABEL: define {{.*}} void @_Z23noexcept_initial_resumev(
// OGCG-EH-NOT:     initresume.eh
// OGCG-EH:       init.ready:
// OGCG-EH-NEXT:    call void @_ZN14NoexceptInitAw12await_resumeEv(ptr {{.*}} %[[INIT:.*]], ptr
// OGCG-EH-NEXT:    call void @_ZN2NTD1Ev(ptr {{.*}} %[[INIT]])
// OGCG-EH-NOT:     initresume.eh
// OGCG-EH:       define

// With a destructor that may throw, a noexcept await_resume() call is still in
// the try/catch, and the result is destroyed before the flag is set.

struct TD {
  int x;
  ~TD() noexcept(false);
};

struct ThrowingDtorInitAw {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  TD await_resume() noexcept;
};

struct ThrowingDtorInitTask {
  struct promise_type {
    ThrowingDtorInitTask get_return_object() noexcept;
    ThrowingDtorInitAw initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

ThrowingDtorInitTask throwing_dtor_initial_resume() {
  next();
  co_return;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z28throwing_dtor_initial_resumev(
// CIR-EH:      %[[RESUME_EH:.*]] = cir.alloca "resume.eh"
// CIR:         cir.await(init, ready : {
// CIR:         }, resume : {
// CIR-EH:          cir.try {
// CIR-EH-NEXT:       cir.call @_ZN18ThrowingDtorInitAw12await_resumeEv(%[[INIT:.*]], %{{.*}})
// CIR-EH-NEXT:       cir.cleanup.scope {
// CIR-EH-NEXT:         cir.yield
// CIR-EH-NEXT:       } cleanup all {
// CIR-EH-NEXT:         cir.call @_ZN2TDD1Ev(%[[INIT]])
// CIR-EH-NEXT:         cir.yield
// CIR-EH-NEXT:       }
// CIR-EH-NEXT:       %[[TRUE:.*]] = cir.const #true
// CIR-EH-NEXT:       cir.store %[[TRUE]], %[[RESUME_EH]]
// CIR-EH-NEXT:       cir.yield
// CIR-EH-NEXT:     } catch all
// CIR:         }, body : {
// CIR-EH-NEXT:   %[[RESUMED:.*]] = cir.load {{.*}} %[[RESUME_EH]]
// CIR-EH-NEXT:   cir.if %[[RESUMED]] {
// CIR-EH:          cir.call @_Z4nextv()

// OGCG-EH-LABEL: define {{.*}} void @_Z28throwing_dtor_initial_resumev(
// OGCG-EH:       init.ready:
// OGCG-EH-NEXT:    store i1 true, ptr %[[RESUME_EH:.*]], align 1
// OGCG-EH-NEXT:    call void @_ZN18ThrowingDtorInitAw12await_resumeEv(ptr {{.*}} %[[INIT:.*]], ptr
// OGCG-EH-NEXT:    invoke void @_ZN2TDD1Ev(ptr {{.*}} %[[INIT]])
// OGCG-EH-NEXT:      to label %[[CONT:.*]] unwind label
// OGCG-EH:       [[CONT]]:
// OGCG-EH-NEXT:    store i1 false, ptr %[[RESUME_EH]], align 1
