// RUN: %clang_cc1 -fopenacc -triple x86_64-linux-gnu -fcxx-exceptions -fexceptions -emit-cir -fclangir %s -o - | FileCheck %s

// Functions that CIRGen creates without a declaration, like the personality
// function and runtime library functions, have no routine directives. Creating
// them with -fopenacc used to crash.

void g();

#pragma acc routine seq
void routine_fn();

void try_catch() {
  try {
    g();
  } catch (...) {
  }
}

// CHECK-LABEL: cir.func {{.*}} @_Z9try_catchv() personality(@__gxx_personality_v0)
// CHECK: cir.func private {{.*}}@__gxx_personality_v0(...) -> !s32i

bool is_lock_free(unsigned long n, void *p) {
  return __atomic_is_lock_free(n, p);
}

// CHECK-LABEL: cir.func {{.*}} @_Z12is_lock_freemPv(
// CHECK:         cir.call @__atomic_is_lock_free(
// CHECK: cir.func private {{.*}}@__atomic_is_lock_free(!u64i, !cir.ptr<!void>)

// The routine directive of a declared function is still handled.
void use_routine() { routine_fn(); }

// CHECK: cir.func {{.*}} @[[ROUTINE:_Z10routine_fnv]]() {{.*}}attributes {acc.routine_info = #acc.routine_info<[@[[ROUTINE_INFO:.*]]]>
// CHECK: acc.routine @[[ROUTINE_INFO]] func(@[[ROUTINE]]) seq
