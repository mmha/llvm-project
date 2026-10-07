// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH

// Destroying a coroutine while it is suspended leaves the scopes around the
// suspend point ([dcl.fct.def.coroutine]p11), and a co_return leaves the
// scopes around it on its way to the final suspend point. The members, bases,
// captures and array elements that an initializer has constructed before such
// a suspend point or co_return are destroyed then, as if the initializer
// threw. Their cleanups are deactivated when the initialization completes, so
// they stay normal cleanups, guarded by a flag that the end of the
// initialization clears, as in classic CodeGen
// (CodeGenCoroutines/coro-suspend-cleanups.cpp).

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

struct Obj {
  Obj(char);
  ~Obj();
};

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
