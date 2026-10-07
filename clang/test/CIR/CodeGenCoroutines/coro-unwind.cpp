// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t-noeh.cir
// RUN: FileCheck --input-file=%t-noeh.cir %s -check-prefix=CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// An exception that escapes a coroutine. Like classic CodeGen, CIR destroys
// what the ramp function has constructed when an exception escapes it while
// it sets up the coroutine state.

#include "Inputs/coroutine.h"

struct Param {
  Param(const Param &);
  ~Param();
};

struct Task {
  ~Task();
  struct promise_type {
    promise_type();
    ~promise_type();
    Task get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception();
  };
};

void may_throw();

Task rethrow(Param p) {
  may_throw();
  co_return;
}

// The parameter copy, the promise and get_return_object() may throw while the
// ramp function sets up the coroutine state. Each of them is followed by a
// cir.cleanup.scope of kind eh that destroys it, and the frame is freed in the
// outermost one.

// CIR-LABEL: cir.func coroutine {{.*}} @_Z7rethrow5Param(
// CIR-SAME:      %[[RETVAL:[^:]*]]: !cir.ptr<!rec_Task>
// CIR:         %[[P:.*]] = cir.alloca "p"
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise"
// CIR:         %[[FRAME:.*]] = cir.coro.intrinsic.begin
// CIR-NEXT:    cir.cleanup.scope {
// CIR-NEXT:      cir.call @_ZN5ParamC1ERKS_(%[[P]],
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.call @_ZN4Task12promise_typeC1Ev(%[[PROMISE]])
// CIR-NEXT:        cir.cleanup.scope {
// CIR-NEXT:          cir.call @_ZN4Task12promise_type17get_return_objectEv(%[[RETVAL]], %[[PROMISE]])
// CIR-NEXT:          cir.yield
// CIR-NEXT:        } cleanup eh {
// CIR-NEXT:          cir.call @_ZN4Task12promise_typeD1Ev(%[[PROMISE]])
// CIR-NEXT:          cir.yield
// CIR-NEXT:        }
// CIR-NEXT:        cir.yield
// CIR-NEXT:      } cleanup eh {
// CIR-NEXT:        cir.call @_ZN5ParamD1Ev(%[[P]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.yield
// CIR-NEXT:    } cleanup eh {
// CIR-NEXT:      %[[MEM:.*]] = cir.coro.intrinsic.free(%{{.*}}, %[[FRAME]])
// CIR:           cir.if
// CIR:             cir.call @_ZdlPvm(%[[MEM]],
// CIR:           cir.coro.intrinsic.dead(%[[FRAME]])
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }
// CIR-NEXT:    cir.coroutine initialSuspend : {

// Without exceptions, there are no cleanups for the setup.

// CIR-NOEH-LABEL: cir.func coroutine {{.*}} @_Z7rethrow5Param(
// CIR-NOEH-NOT:   cir.cleanup.scope
// CIR-NOEH:       cir.coroutine initialSuspend : {

// LLVM-LABEL: define dso_local void @_Z7rethrow5Param(
// LLVM-SAME:      ptr {{.*}} sret(%struct.Task) {{.*}} %[[RETVAL:[0-9]+]], ptr {{.*}} %[[ARG:[0-9]+]])
// LLVM:         %[[P:.*]] = alloca %struct.Param
// LLVM:         %[[PROMISE:.*]] = alloca %"struct.Task::promise_type"
// LLVM:         %[[ID:.*]] = call token @llvm.coro.id(
// LLVM:         %[[FRAME:.*]] = call ptr @llvm.coro.begin(
// LLVM:         invoke void @_ZN5ParamC1ERKS_(ptr {{.*}} %[[P]], ptr {{.*}} %[[ARG]])
// LLVM-NEXT:      to label %{{.*}} unwind label %[[COPY_LPAD:[0-9]+]]
// LLVM:         invoke void @_ZN4Task12promise_typeC1Ev(ptr {{.*}} %[[PROMISE]])
// LLVM-NEXT:      to label %{{.*}} unwind label %[[PROMISE_LPAD:[0-9]+]]
// LLVM:         invoke void @_ZN4Task12promise_type17get_return_objectEv(ptr {{.*}} %[[RETVAL]], ptr {{.*}} %[[PROMISE]])
// LLVM-NEXT:      to label %{{.*}} unwind label %[[GRO_LPAD:[0-9]+]]
// LLVM:       [[GRO_LPAD]]:
// LLVM-NEXT:    landingpad { ptr, i32 }
// LLVM-NEXT:      cleanup
// LLVM:         call void @_ZN4Task12promise_typeD1Ev(ptr {{.*}} %[[PROMISE]])
// LLVM-NEXT:    br label %[[DESTROY_P:[0-9]+]]
// LLVM:       [[PROMISE_LPAD]]:
// LLVM-NEXT:    landingpad { ptr, i32 }
// LLVM-NEXT:      cleanup
// LLVM:       [[DESTROY_P]]:
// LLVM:         call void @_ZN5ParamD1Ev(ptr {{.*}} %[[P]])
// LLVM-NEXT:    br label %[[FREE:[0-9]+]]
// LLVM:       [[COPY_LPAD]]:
// LLVM-NEXT:    landingpad { ptr, i32 }
// LLVM-NEXT:      cleanup
// LLVM:       [[FREE]]:
// LLVM:         %[[MEM:.*]] = call ptr @llvm.coro.free(token %[[ID]], ptr %[[FRAME]])
// LLVM:         call void @_ZdlPvm(ptr noundef %[[MEM]],
// LLVM:         call void @llvm.coro.dead(ptr %[[FRAME]])
// LLVM:         resume { ptr, i32 }

// Classic CodeGen guards the destruction of the return value with a flag,
// since its exceptional cleanups for the setup are on the EH stack before
// get_return_object() is called.

// OGCG-LABEL: define dso_local void @_Z7rethrow5Param(
// OGCG:         invoke void @_ZN5ParamC1ERKS_(
// OGCG-NEXT:      to label %{{.*}} unwind label %[[COPY_LPAD:.*]]
// OGCG:         invoke void @_ZN4Task12promise_typeC1Ev(ptr {{.*}} %__promise)
// OGCG-NEXT:      to label %{{.*}} unwind label %[[PROMISE_LPAD:.*]]
// OGCG:         store i1 false, ptr %[[ACTIVE:coro.result.active]]
// OGCG-NEXT:    invoke void @_ZN4Task12promise_type17get_return_objectEv(ptr {{.*}} %agg.result, ptr {{.*}} %__promise)
// OGCG-NEXT:      to label %{{.*}} unwind label %[[GRO_LPAD:.*]]
// OGCG:         store i1 true, ptr %[[ACTIVE]]
// OGCG:       [[COPY_LPAD]]:
// OGCG:         br label %[[FREE:.*]]
// OGCG:       [[PROMISE_LPAD]]:
// OGCG:         br label %[[DESTROY_P:.*]]
// OGCG:       [[GRO_LPAD]]:
// OGCG:         br label %[[DESTROY_RESULT:.*]]
// OGCG:       [[DESTROY_RESULT]]:
// OGCG-NEXT:    %[[IS_ACTIVE:.*]] = load i1, ptr %[[ACTIVE]]
// OGCG-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:.*]], label %[[DONE:.*]]
// OGCG:       [[DTOR]]:
// OGCG-NEXT:    call void @_ZN4TaskD1Ev(ptr {{.*}} %agg.result)
// OGCG:       [[DONE]]:
// OGCG-NEXT:    call void @_ZN4Task12promise_typeD1Ev(ptr {{.*}} %__promise)
// OGCG-NEXT:    br label %[[DESTROY_P]]
// OGCG:       [[DESTROY_P]]:
// OGCG:         call void @_ZN5ParamD1Ev(
// OGCG-NEXT:    br label %[[FREE]]
// OGCG:       [[FREE]]:
// OGCG:         call ptr @llvm.coro.free(
// OGCG:         call void @_ZdlPvm(
// OGCG:         call void @llvm.coro.dead(
