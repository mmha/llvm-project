// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM

// FIXME: An exception that leaves the coroutine, e.g. one that
// unhandled_exception() rethrows, does not end the coroutine with
// coro.end(unwind=true) yet, so the LLVM checks do not cover that path and
// there is no comparison with classic CodeGen.

#include "Inputs/coroutine.h"

TaskWithEH simple_eh_body() {
  co_return;
}

// CIR-LABEL: cir.func{{.*}} @_Z14simple_eh_bodyv
// CIR: cir.coroutine initialSuspend : {
// CIR: }, resume : {
// Note no 'try'/'catch' here.
// CIR-NEXT: cir.call @_ZNSt15suspend_nothrow12await_resumeEv
// CIR-NEXT: cir.yield
// CIR-NEXT: }

// CIR: }, body : {
// CIR-NOT: cir.if
// CIR:   cir.scope {
// CIR:     cir.try {
// CIR:       cir.call @_ZN10TaskWithEH12promise_type11return_voidEv
// CIR:       cir.co_return
// CIR:     } catch all {{.*}} {
// CIR:       cir.cleanup.scope {
// CIR:         cir.call @_ZN10TaskWithEH12promise_type19unhandled_exceptionEv
// CIR:         cir.yield
// CIR:       } cleanup all {
// CIR:         cir.end_catch
// CIR:         cir.yield
// CIR:       }
// CIR:       cir.yield
// CIR:     }
// CIR:   }
// CIR:   cir.yield
// Make sure that 'final' is outside of the above try/catch/etc.
// CIR: }, finalSuspend : {
// CIR: cir.call @_ZN10TaskWithEH12promise_type13final_suspendEv

// The implicit handler catches every exception from the body, calls
// unhandled_exception() and continues with the final suspend point.

// LLVM-LABEL: define {{.*}} @_Z14simple_eh_bodyv(
// LLVM:         call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM:         invoke void @_ZN10TaskWithEH12promise_type11return_voidEv(
// LLVM-NEXT:      to label %[[CONT:.*]] unwind label %[[LPAD:.*]]
// LLVM:       [[CONT]]:
// LLVM-NEXT:    br label %[[FINAL:.*]]
// LLVM:       [[LPAD]]:
// LLVM-NEXT:    landingpad { ptr, i32 }
// LLVM-NEXT:      catch ptr null
// LLVM:         call ptr @__cxa_begin_catch(
// LLVM:         invoke void @_ZN10TaskWithEH12promise_type19unhandled_exceptionEv(
// LLVM:         call void @__cxa_end_catch()
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN10TaskWithEH12promise_type13final_suspendEv(
// LLVM:         call i8 @llvm.coro.suspend(token %{{.*}}, i1 true)

TaskThrowingInit throwing_init_suspend() {
  co_return;
}

// CIR-LABEL: cir.func{{.*}} @_Z21throwing_init_suspendv
// CIR: %[[RESUME_FLAG:.*]] = cir.alloca "resume.eh" align(1) : !cir.ptr<!cir.bool>
// CIR: cir.coroutine initialSuspend : {
// CIR: }, resume : {
// CIR:    %[[FALSE:.*]] = cir.const #false
// CIR:    cir.store %[[FALSE]], %[[RESUME_FLAG]] : !cir.bool, !cir.ptr<!cir.bool>
// CIR:    cir.scope {
// CIR:      cir.try {
// CIR:        cir.call @_ZNSt19suspend_maybe_throw12await_resumeEv
// CIR:        %[[TRUE:.*]] = cir.const #true
// CIR:        cir.store %[[TRUE]], %[[RESUME_FLAG]] : !cir.bool, !cir.ptr<!cir.bool>
// CIR:        cir.yield
// CIR:      } catch all {{.*}} {
// CIR:        cir.begin_catch
// CIR:        cir.cleanup.scope {
// CIR:          cir.call @_ZN16TaskThrowingInit12promise_type19unhandled_exceptionEv
// CIR:          cir.yield
// CIR:        } cleanup all {
// CIR:          cir.end_catch
// CIR:          cir.yield
// CIR:        }
// CIR:        cir.yield
// CIR:      }
// CIR:    }
// CIR:    cir.yield
// CIR: }

// CIR: }, body : {
// CIR:   %[[LOAD_RESUME_FLAG:.*]] = cir.load align(1) %[[RESUME_FLAG]] : !cir.ptr<!cir.bool>, !cir.bool
// CIR:   cir.if %[[LOAD_RESUME_FLAG]] {
// CIR:     cir.scope {
// CIR:       cir.try {
// CIR:         cir.call @_ZN16TaskThrowingInit12promise_type11return_voidEv
// CIR:         cir.co_return
// CIR:       } catch all {{.*}} {
// CIR:         cir.cleanup.scope {
// CIR:           cir.call @_ZN16TaskThrowingInit12promise_type19unhandled_exceptionEv
// CIR:           cir.yield
// CIR:         } cleanup all {
// CIR:           cir.end_catch
// CIR:           cir.yield
// CIR:         }
// CIR:         cir.yield
// CIR:       }
// CIR:     }
// CIR:   }
// CIR:   cir.yield
// CIR: }, finalSuspend : {
// CIR: cir.call @_ZN16TaskThrowingInit12promise_type13final_suspendEv

// An exception from the initial await_resume is passed to
// unhandled_exception() too, and then the body is skipped.

// LLVM-LABEL: define {{.*}} @_Z21throwing_init_suspendv(
// LLVM:         %[[RESUME_FLAG:.*]] = alloca i8
// LLVM:         call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM:         store i8 0, ptr %[[RESUME_FLAG]]
// LLVM:         invoke void @_ZNSt19suspend_maybe_throw12await_resumeEv(
// LLVM-NEXT:      to label %[[RESUMED:.*]] unwind label %[[LPAD:.*]]
// LLVM:       [[RESUMED]]:
// LLVM-NEXT:    store i8 1, ptr %[[RESUME_FLAG]]
// LLVM:       [[LPAD]]:
// LLVM-NEXT:    landingpad { ptr, i32 }
// LLVM-NEXT:      catch ptr null
// LLVM:         invoke void @_ZN16TaskThrowingInit12promise_type19unhandled_exceptionEv(
// LLVM:         %[[FLAG:.*]] = load i8, ptr %[[RESUME_FLAG]]
// LLVM-NEXT:    %[[RAN:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[RAN]], label %[[BODY:.*]], label %{{.*}}
// LLVM:       [[BODY]]:
// LLVM:         invoke void @_ZN16TaskThrowingInit12promise_type11return_voidEv(
// LLVM:         call void @_ZN16TaskThrowingInit12promise_type13final_suspendEv(
// LLVM:         call i8 @llvm.coro.suspend(token %{{.*}}, i1 true)
