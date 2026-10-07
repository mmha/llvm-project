// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir-eh.ll
// RUN: FileCheck --input-file=%t-cir-eh.ll %s -check-prefix=LLVM-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// The cleanups of the scopes around a suspend point run when the coroutine is
// destroyed while suspended there ([dcl.fct.def.coroutine]p11), innermost
// first, before the promise is destroyed and the frame is freed. They do not
// run when the coroutine suspends and returns to its caller or resumer
// ([expr.await]p5.1, [stmt.jump.general]p2). A co_return runs the cleanups of
// the scopes it leaves before the final suspend point.
//
// The members, bases, captures and array elements that an initializer has
// constructed before such a suspend point or co_return are destroyed then as
// well, as if the initializer threw. Their cleanups are deactivated when the
// initialization completes, so they stay normal cleanups, guarded by a flag
// that the end of the initialization clears, as in classic CodeGen
// (CodeGenCoroutines/coro-suspend-cleanups.cpp).

#include "Inputs/coroutine.h"

struct ValueTask {
  struct promise_type {
    ValueTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_value(int) noexcept;
    void unhandled_exception() noexcept;
  };
};

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

struct Obj {
  Obj() = default;
  Obj(char);
  ~Obj();
};

void may_throw();

struct Pair {
  Obj a;
  Obj b;
};

struct Derived : Obj {
  Obj m;
};

struct ObjAwaiter : std::suspend_always {
  Obj await_resume();
};

ValueTask scopes(bool b) {
  Obj outer;
  {
    Obj inner;
    co_await std::suspend_always{};
    if (b)
      co_return 1;
  }
  co_return 2;
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z6scopesb(
// CIR-NOEH:         }, body : {
// CIR-NOEH-NEXT:      cir.cleanup.scope {
// CIR-NOEH-NEXT:        cir.scope {
// CIR-NOEH-NEXT:          %[[INNER:.*]] = cir.alloca "inner"
// CIR-NOEH:               cir.cleanup.scope {
// CIR-NOEH-NEXT:            cir.await(user,
// CIR-NOEH:                 cir.if
// CIR-NOEH:                   cir.call @_ZN9ValueTask12promise_type12return_valueEi(
// CIR-NOEH-NEXT:              cir.co_return
// CIR-NOEH:               } cleanup normal {
// CIR-NOEH-NEXT:            cir.call @_ZN3ObjD1Ev(%[[INNER]])
// CIR-NOEH:             cir.call @_ZN9ValueTask12promise_type12return_valueEi(
// CIR-NOEH-NEXT:        cir.co_return
// CIR-NOEH-NEXT:      ^bb1:
// CIR-NOEH-NEXT:        cir.yield
// CIR-NOEH-NEXT:      } cleanup normal {
// CIR-NOEH-NEXT:        cir.call @_ZN3ObjD1Ev(%[[OUTER:.*]])
// CIR-NOEH:         }, finalSuspend : {

// The cleanup destination slot holds where to continue after each cleanup:
// the destroy path, one of the co_returns, or the end of the scope.

// LLVM-LABEL: define dso_local void @_Z6scopesb(
// LLVM:         %[[SLOT:.*]] = alloca i32
//
// The user co_await. A destroyed coroutine runs the cleanup of inner first, a
// suspended one returns right away.
//
// LLVM:         call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z6scopesb.__await_suspend_wrapper__await)
// LLVM-NEXT:    %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %[[SUSPENDED:.*]] [
// LLVM-NEXT:      i8 0, label %[[RESUMED:.*]]
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[INNER_CLEANUP:.*]]
// LLVM:       [[SUSPENDED]]:
// LLVM-NEXT:    br label %[[RET:.*]]
// LLVM:       [[RESUMED]]:
// LLVM-NEXT:    call void @_ZNSt14suspend_always12await_resumeEv(
//
// co_return 1 leaves both scopes, through the same cleanups.
//
// LLVM:         call void @_ZN9ValueTask12promise_type12return_valueEi(ptr {{.*}}, i32 noundef 1)
// LLVM-NEXT:    store i32 2, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[INNER_CLEANUP]]
//
// LLVM:       [[INNER_CLEANUP]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[INNER:[^)]*]])
// LLVM-NEXT:    br label %[[INNER_DISPATCH:.*]]
// LLVM:       [[INNER_DISPATCH]]:
// LLVM-NEXT:    %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %[[INNER_END:.*]]
// LLVM-NEXT:      i32 1, label %[[INNER_TO_DESTROY:.*]]
// LLVM-NEXT:      i32 2, label %[[INNER_TO_FINAL:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[INNER_TO_DESTROY]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[OUTER_CLEANUP:.*]]
// LLVM:       [[INNER_TO_FINAL]]:
// LLVM-NEXT:    store i32 2, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[OUTER_CLEANUP]]
//
// co_return 2 leaves the outer scope. The unreachable end of the outer scope
// after it is a fall-through exit without predecessors.
//
// LLVM:         call void @_ZN9ValueTask12promise_type12return_valueEi(ptr {{.*}}, i32 noundef 2)
// LLVM-NEXT:    store i32 3, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[OUTER_CLEANUP]]
//
// LLVM:       [[OUTER_CLEANUP]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[OUTER:[^)]*]])
// LLVM-NEXT:    br label %[[OUTER_DISPATCH:.*]]
// LLVM:       [[OUTER_DISPATCH]]:
// LLVM-NEXT:    %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[OUTER_TO_DESTROY:.*]]
// LLVM-NEXT:      i32 2, label %[[OUTER_TO_FINAL:.*]]
// LLVM-NEXT:      i32 3, label %[[OUTER_TO_FINAL2:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[OUTER_TO_DESTROY]]:
// LLVM-NEXT:    br label %[[CLEANUP:.*]]
// LLVM:       [[OUTER_TO_FINAL]]:
// LLVM-NEXT:    br label %[[FINAL:.*]]
// LLVM:       [[OUTER_TO_FINAL2]]:
// LLVM-NEXT:    br label %[[FINAL]]
//
// The final suspend point.
//
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN9ValueTask12promise_type13final_suspendEv(
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 true)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %[[FINAL_SUSPENDED:.*]] [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[FINAL_DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[FINAL_DESTROYED]]:
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[FINAL_SUSPENDED]]:
// LLVM-NEXT:    br label %[[RET]]
//
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(
// LLVM:         call void @llvm.coro.dead(
// LLVM:       [[RET]]:
// LLVM-NEXT:    call void @llvm.coro.end(ptr null, i1 false, token none)
// LLVM-NEXT:    load %struct.ValueTask
// LLVM-NEXT:    ret void

// OGCG-LABEL: define dso_local void @_Z6scopesb(
// OGCG:       await.suspend:
// OGCG:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// OGCG-NEXT:    switch i8 %[[SUSPEND]], label %coro.ret [
// OGCG-NEXT:      i8 0, label %await.ready
// OGCG-NEXT:      i8 1, label %await.cleanup
// OGCG-NEXT:    ]
// OGCG:       if.then:
// OGCG-NEXT:    call void @_ZN9ValueTask12promise_type12return_valueEi(ptr {{.*}}, i32 noundef 1)
// OGCG:         call void @_ZN3ObjD1Ev(ptr {{.*}} %inner)
// OGCG:         call void @_ZN9ValueTask12promise_type12return_valueEi(ptr {{.*}}, i32 noundef 2)
// OGCG:         call void @_ZN3ObjD1Ev(ptr {{.*}} %outer)
// OGCG:         switch i32 %{{.*}}, label %unreachable [
// OGCG-NEXT:      i32 2, label %coro.cleanup
// OGCG-NEXT:      i32 3, label %coro.final
// OGCG-NEXT:    ]
// OGCG:       coro.final:
// OGCG:         call i8 @llvm.coro.suspend(token %{{.*}}, i1 true)
// OGCG:       coro.cleanup:
// OGCG:         call ptr @llvm.coro.free(
// OGCG:       coro.ret:
// OGCG-NEXT:    call void @llvm.coro.end(ptr null, i1 false, token none)

// With exceptions, the cleanups are of kind all. The destroy edge runs their
// normal copies, and a call that throws unwinds through their EH copies to
// the handler that calls unhandled_exception().

ValueTask scopes_eh() {
  Obj outer;
  {
    Obj inner;
    may_throw();
    co_await std::suspend_always{};
    may_throw();
  }
  co_return 1;
}

// LLVM-EH-LABEL: define dso_local void @_Z9scopes_ehv(
// LLVM-EH:         invoke void @_Z9may_throwv()
// LLVM-EH-NEXT:            to label %{{.*}} unwind label %[[LPAD:[0-9]+]]
// LLVM-EH:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-EH-NEXT:    switch i8 %[[SUSPEND]], label %[[SUSPENDED:.*]] [
// LLVM-EH-NEXT:      i8 0, label %[[RESUMED:.*]]
// LLVM-EH-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-EH-NEXT:    ]
// LLVM-EH:       [[DESTROYED]]:
// LLVM-EH-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-EH-NEXT:    br label %[[INNER_CLEANUP:[0-9]+]]
// LLVM-EH:       [[SUSPENDED]]:
// LLVM-EH-NEXT:    br label %[[RET:[0-9]+]]
// LLVM-EH:       [[RESUMED]]:
// LLVM-EH-NEXT:    call void @_ZNSt14suspend_always12await_resumeEv(
// LLVM-EH:         invoke void @_Z9may_throwv()
// LLVM-EH-NEXT:            to label %{{.*}} unwind label %[[LPAD]]
// LLVM-EH:       [[INNER_CLEANUP]]:
// LLVM-EH-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[INNER:[0-9]+]])
// LLVM-EH:       [[LPAD]]:
// LLVM-EH-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// LLVM-EH-NEXT:            catch ptr null
// LLVM-EH:         call void @_ZN3ObjD1Ev(ptr {{.*}} %[[INNER]])
// LLVM-EH-NEXT:    br label %[[EH_OUTER:[0-9]+]]
// LLVM-EH:       [[EH_OUTER]]:
// LLVM-EH:         call void @_ZN3ObjD1Ev(ptr {{.*}} %[[OUTER:[0-9]+]])
// LLVM-EH:         call ptr @__cxa_begin_catch(
// LLVM-EH-NEXT:    br label %{{.*}}
// LLVM-EH:         call void @_ZN9ValueTask12promise_type19unhandled_exceptionEv(
// LLVM-EH:       [[RET]]:
// LLVM-EH-NEXT:    call void @llvm.coro.end(ptr null, i1 false, token none)

Task aggregate_member() { Pair p{Obj{'a'}, co_await ObjAwaiter{}}; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z16aggregate_memberv(
// CIR:         }, body : {
// CIR:           %[[A:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_Pair> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[A]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.await(user,
// CIR:           %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[A]])

// The destroy edge of the co_await branches through the cleanup of p.a, which
// tests the flag, and on to the destroy region.
//
// LLVM-LABEL: define dso_local void @_Z16aggregate_memberv(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[P:[0-9]+]], i8 noundef signext 97)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[P]])
// LLVM:         %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[TO_DESTROY:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[TO_DESTROY]]:
// LLVM-NEXT:    br label %[[DESTROY:[0-9]+]]
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(

// OGCG-LABEL: define dso_local void @_Z16aggregate_memberv(
// OGCG:         call void @_ZN3ObjC1Ec(ptr {{.*}} %p, i8 noundef signext 97)
// OGCG-NEXT:    store i1 true, ptr %cleanup.isactive
// OGCG:       await.cleanup:
// OGCG-NEXT:    br label %[[CLEANUP:.*]]
// OGCG:       await.ready:
// OGCG:         store i1 false, ptr %cleanup.isactive
// OGCG:       [[CLEANUP]]:
// OGCG:         %cleanup.is_active = load i1, ptr %cleanup.isactive
// OGCG-NEXT:    br i1 %cleanup.is_active, label %cleanup.action, label %cleanup.done
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %p)

Task base() { Derived d{Obj{'b'}, co_await ObjAwaiter{}}; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z4basev(
// CIR:         }, body : {
// CIR:           %[[BASE:.*]] = cir.base_class_addr nonnull %{{.*}} [0] : !cir.ptr<!rec_Derived> -> !cir.ptr<!rec_Obj>
// CIR:           %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.await(user,
// CIR:           %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[BASE]])

// The destroy edge destroys the base, then the temporary it was copied from.
//
// LLVM-LABEL: define dso_local void @_Z4basev(
// LLVM:         %[[D:[0-9]+]] = alloca %struct.Derived
// LLVM-NEXT:    %[[TMP:[0-9]+]] = alloca %struct.Obj
// LLVM:         store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[D]])
// LLVM:         call void @_ZN3ObjD1Ev(ptr {{.*}} %[[TMP]])

Task lambda_capture() {
  auto l = [o = Obj{'c'}, p = co_await ObjAwaiter{}] {};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z14lambda_capturev(
// CIR:         }, body : {
// CIR:           %[[O:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_anon{{.*}}> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[O]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.await(user,
// CIR:           %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[O]])

// LLVM-LABEL: define dso_local void @_Z14lambda_capturev(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[L:[0-9]+]], i8 noundef signext 99)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[L]])

Task lifetime_extended() {
  const Pair &r = Pair{Obj{'d'}, co_await ObjAwaiter{}};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17lifetime_extendedv(
// CIR:         }, body : {
// CIR:           %[[A:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_Pair> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[A]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.await(user,
// CIR:           %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[A]])

// LLVM-LABEL: define dso_local void @_Z17lifetime_extendedv(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[R:[0-9]+]], i8 noundef signext 100)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[R]])

Task array() { Obj arr[2] = {Obj{'e'}, co_await ObjAwaiter{}}; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z5arrayv(
// CIR:         }, body : {
// CIR:           %[[BEGIN:.*]] = cir.cast array_to_ptrdecay
// CIR-NEXT:      cir.store {{.*}} %[[BEGIN]], %[[END:.*]] : !cir.ptr<!rec_Obj>, !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.call @_ZN3ObjC1Ec(%[[BEGIN]],
// CIR:             cir.await(user,
// CIR:           %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.load {{.*}} %[[END]]
// CIR:               cir.do {
// CIR:                 cir.call @_ZN3ObjD1Ev(

// The destroy edge destroys the elements up to the end of the initialized
// part.
//
// LLVM-LABEL: define dso_local void @_Z5arrayv(
// LLVM:         %[[BEGIN:[0-9]+]] = getelementptr %struct.Obj, ptr %{{.*}}, i32 0
// LLVM-NEXT:    store ptr %[[BEGIN]], ptr %[[END:[0-9]+]]
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[BEGIN]], i8 noundef signext 101)
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[ACTION:[0-9]+]], label %{{.*}}
// LLVM:       [[ACTION]]:
// LLVM-NEXT:    %[[CUR_END:.*]] = load ptr, ptr %[[END]]
// LLVM-NEXT:    %{{.*}} = icmp ne ptr %[[CUR_END]], %[[BEGIN]]
// LLVM:         call void @_ZN3ObjD1Ev(

// OGCG-LABEL: define dso_local void @_Z5arrayv(
// OGCG:         store i1 true, ptr %cleanup.isactive
// OGCG:       await.cleanup:
// OGCG-NEXT:    br label %[[CLEANUP:.*]]
// OGCG:       await.ready:
// OGCG:         store i1 false, ptr %cleanup.isactive
// OGCG:       [[CLEANUP]]:
// OGCG:         %cleanup.is_active = load i1, ptr %cleanup.isactive
// OGCG-NEXT:    br i1 %cleanup.is_active, label %cleanup.action, label %cleanup.done
// OGCG:       cleanup.action:
// OGCG:         %arraydestroy.isempty = icmp eq ptr %arr, %{{.*}}

Task co_return_in_initializer(bool b) {
  Pair p{Obj{'f'}, ({
           if (b)
             co_return;
           Obj{'g'};
         })};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z24co_return_in_initializerb(
// CIR:         }, body : {
// CIR:           cir.call @_ZN3ObjC1Ec(%[[A:[^,]*]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.if
// CIR:               cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR-NEXT:          cir.co_return
// CIR:           %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[A]])

// The co_return branches through the cleanup of p.a to the final suspend
// point.
//
// LLVM-LABEL: define dso_local void @_Z24co_return_in_initializerb(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[P:[0-9]+]], i8 noundef signext 102)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         call void @_ZN4Task12promise_type11return_voidEv(
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[P]])
// LLVM:         %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[TO_FINAL:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[TO_FINAL]]:
// LLVM-NEXT:    br label %[[FINAL:[0-9]+]]
// LLVM:       [[FINAL]]:
// LLVM-NEXT:    call void @_ZN4Task12promise_type13final_suspendEv(

// The suspend point can also be in an operand of a conditional or logical
// operator. The cleanup scope that the operator opens for its operands is
// still open when the initialization completes, and the flag is cleared there
// (CodeGen/cleanup-deactivation-in-conditional.cpp).

struct BoolAwaiter : std::suspend_always {
  bool await_resume();
};

Obj mk(bool);

Task aggregate_ternary(bool b) {
  Pair p{Obj{'h'}, b ? co_await ObjAwaiter{} : Obj{'i'}};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17aggregate_ternaryb(
// CIR:         }, body : {
// CIR:           %[[A:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_Pair> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[A]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.if
// CIR:               cir.await(user,
// CIR:             } else {
// CIR:               cir.call @_ZN3ObjC1Ec(
// CIR-NEXT:        } loc(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[A]])

// As in aggregate_member, the destroy edge of the co_await branches through
// the flag-guarded cleanup of p.a to the destroy region, and the end of the
// conditional operator clears the flag.
//
// LLVM-LABEL: define dso_local void @_Z17aggregate_ternaryb(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[P:[0-9]+]], i8 noundef signext 104)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[P]])
// LLVM:         %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[TO_DESTROY:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[TO_DESTROY]]:
// LLVM-NEXT:    br label %[[DESTROY:[0-9]+]]
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(

// OGCG-LABEL: define dso_local void @_Z17aggregate_ternaryb(
// OGCG:         call void @_ZN3ObjC1Ec(ptr {{.*}} %p, i8 noundef signext 104)
// OGCG-NEXT:    store i1 true, ptr %cleanup.isactive
// OGCG:       await.cleanup:
// OGCG-NEXT:    br label %[[CLEANUP:.*]]
// OGCG:       cond.end:
// OGCG-NEXT:    store i1 false, ptr %cleanup.isactive
// OGCG:       [[CLEANUP]]:
// OGCG:         %cleanup.is_active = load i1, ptr %cleanup.isactive
// OGCG-NEXT:    br i1 %cleanup.is_active, label %cleanup.action, label %cleanup.done
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %p)

Task aggregate_logical_and(bool b) {
  Pair p{Obj{'j'}, mk(b && co_await BoolAwaiter{})};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z21aggregate_logical_andb(
// CIR:         }, body : {
// CIR:           %[[A:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_Pair> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[A]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.ternary(
// CIR:               cir.await(user,
// CIR:             cir.call @_Z2mkb(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[A]])

// LLVM-LABEL: define dso_local void @_Z21aggregate_logical_andb(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[P:[0-9]+]], i8 noundef signext 106)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[P]])
// LLVM:         %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[TO_DESTROY:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[TO_DESTROY]]:
// LLVM-NEXT:    br label %[[DESTROY:[0-9]+]]
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(

Task array_ternary(bool b) {
  Obj arr[2] = {Obj{'k'}, b ? co_await ObjAwaiter{} : Obj{'l'}};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z13array_ternaryb(
// CIR:         }, body : {
// CIR:           %[[BEGIN:.*]] = cir.cast array_to_ptrdecay
// CIR-NEXT:      cir.store {{.*}} %[[BEGIN]], %[[END:.*]] : !cir.ptr<!rec_Obj>, !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.call @_ZN3ObjC1Ec(%[[BEGIN]],
// CIR:             cir.if
// CIR:               cir.await(user,
// CIR:             } else {
// CIR:               cir.call @_ZN3ObjC1Ec(
// CIR-NEXT:        } loc(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.load {{.*}} %[[END]]
// CIR:               cir.do {
// CIR:                 cir.call @_ZN3ObjD1Ev(

// LLVM-LABEL: define dso_local void @_Z13array_ternaryb(
// LLVM:         %[[BEGIN:[0-9]+]] = getelementptr %struct.Obj, ptr %{{.*}}, i32 0
// LLVM-NEXT:    store ptr %[[BEGIN]], ptr %[[END:[0-9]+]]
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[BEGIN]], i8 noundef signext 107)
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[ACTION:[0-9]+]], label %{{.*}}
// LLVM:       [[ACTION]]:
// LLVM-NEXT:    %[[CUR_END:.*]] = load ptr, ptr %[[END]]
// LLVM-NEXT:    %{{.*}} = icmp ne ptr %[[CUR_END]], %[[BEGIN]]
// LLVM:         call void @_ZN3ObjD1Ev(

Task array_logical_and(bool b) {
  Obj arr[2] = {Obj{'m'}, mk(b && co_await BoolAwaiter{})};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z17array_logical_andb(
// CIR:         }, body : {
// CIR:           %[[BEGIN:.*]] = cir.cast array_to_ptrdecay
// CIR-NEXT:      cir.store {{.*}} %[[BEGIN]], %[[END:.*]] : !cir.ptr<!rec_Obj>, !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.call @_ZN3ObjC1Ec(%[[BEGIN]],
// CIR:             cir.ternary(
// CIR:               cir.await(user,
// CIR:             cir.call @_Z2mkb(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.load {{.*}} %[[END]]
// CIR:               cir.do {
// CIR:                 cir.call @_ZN3ObjD1Ev(

// LLVM-LABEL: define dso_local void @_Z17array_logical_andb(
// LLVM:         %[[BEGIN:[0-9]+]] = getelementptr %struct.Obj, ptr %{{.*}}, i32 0
// LLVM-NEXT:    store ptr %[[BEGIN]], ptr %[[END:[0-9]+]]
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[BEGIN]], i8 noundef signext 109)
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[ACTION:[0-9]+]], label %{{.*}}
// LLVM:       [[ACTION]]:
// LLVM-NEXT:    %[[CUR_END:.*]] = load ptr, ptr %[[END]]
// LLVM-NEXT:    %{{.*}} = icmp ne ptr %[[CUR_END]], %[[BEGIN]]
// LLVM:         call void @_ZN3ObjD1Ev(

Task lambda_capture_ternary(bool b) {
  auto l = [o = Obj{'n'}, p = b ? co_await ObjAwaiter{} : Obj{'o'}] {};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z22lambda_capture_ternaryb(
// CIR:         }, body : {
// CIR:           %[[O:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_anon{{.*}}> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[O]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.if
// CIR:               cir.await(user,
// CIR:             } else {
// CIR:               cir.call @_ZN3ObjC1Ec(
// CIR-NEXT:        } loc(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[O]])

// LLVM-LABEL: define dso_local void @_Z22lambda_capture_ternaryb(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[L:[0-9]+]], i8 noundef signext 110)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[L]])
// LLVM:         %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[TO_DESTROY:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[TO_DESTROY]]:
// LLVM-NEXT:    br label %[[DESTROY:[0-9]+]]
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(

Task lambda_capture_logical_and(bool b) {
  auto l = [o = Obj{'p'}, p = mk(b && co_await BoolAwaiter{})] {};
}

// CIR-LABEL: cir.func coroutine {{.*}} @_Z26lambda_capture_logical_andb(
// CIR:         }, body : {
// CIR:           %[[O:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_anon{{.*}}> -> !cir.ptr<!rec_Obj>
// CIR:           cir.call @_ZN3ObjC1Ec(%[[O]],
// CIR-NEXT:      %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:      cir.store %[[TRUE]], %[[ACTIVE:.*]] : !cir.bool
// CIR-NEXT:      cir.cleanup.scope {
// CIR:             cir.ternary(
// CIR:               cir.await(user,
// CIR:             cir.call @_Z2mkb(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:        cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[O]])

// LLVM-LABEL: define dso_local void @_Z26lambda_capture_logical_andb(
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}} %[[L:[0-9]+]], i8 noundef signext 112)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         %[[SUSPEND:.*]] = call i8 @llvm.coro.suspend(token %{{.*}}, i1 false)
// LLVM-NEXT:    switch i8 %[[SUSPEND]], label %{{.*}} [
// LLVM-NEXT:      i8 0, label %{{.*}}
// LLVM-NEXT:      i8 1, label %[[DESTROYED:.*]]
// LLVM-NEXT:    ]
// LLVM:       [[DESTROYED]]:
// LLVM-NEXT:    store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DTOR:[0-9]+]], label %{{.*}}
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[L]])
// LLVM:         %[[DEST:.*]] = load i32, ptr %[[SLOT]]
// LLVM-NEXT:    switch i32 %[[DEST]], label %{{.*}} [
// LLVM-NEXT:      i32 0, label %{{.*}}
// LLVM-NEXT:      i32 1, label %[[TO_DESTROY:[0-9]+]]
// LLVM-NEXT:    ]
// LLVM:       [[TO_DESTROY]]:
// LLVM-NEXT:    br label %[[DESTROY:[0-9]+]]
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(
