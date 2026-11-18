// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-lowering-prepare %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --input-file=%t-before.cir %s --check-prefix=CIR-BEFORE-LPP
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

struct NeedsCtor {
  NeedsCtor();
};

struct Holder {
  static NeedsCtor first;
  static NeedsCtor second;
};

NeedsCtor *p = &Holder::first;

NeedsCtor Holder::second;
NeedsCtor Holder::first;

// CIR-BEFORE-LPP: cir.global external @p = #cir.global_view<@_ZN6Holder5firstE> : !cir.ptr<!rec_NeedsCtor>
// CIR-BEFORE-LPP: cir.global external @_ZN6Holder6secondE = #cir.zero : !rec_NeedsCtor ctor {
// CIR-BEFORE-LPP:   %[[THIS:.*]] = cir.get_global @_ZN6Holder6secondE : !cir.ptr<!rec_NeedsCtor>
// CIR-BEFORE-LPP:   cir.call @_ZN9NeedsCtorC1Ev(%[[THIS]])
// CIR-BEFORE-LPP: }
// CIR-BEFORE-LPP: cir.global external @_ZN6Holder5firstE = #cir.zero : !rec_NeedsCtor ctor {
// CIR-BEFORE-LPP:   %[[THIS:.*]] = cir.get_global @_ZN6Holder5firstE : !cir.ptr<!rec_NeedsCtor>
// CIR-BEFORE-LPP:   cir.call @_ZN9NeedsCtorC1Ev(%[[THIS]])
// CIR-BEFORE-LPP: }

// CIR: cir.global external @p = #cir.global_view<@_ZN6Holder5firstE> : !cir.ptr<!rec_NeedsCtor>
// CIR: cir.global external @_ZN6Holder6secondE = #cir.zero : !rec_NeedsCtor
// CIR: cir.func internal private @__cxx_global_var_init(){{.*}} {
// CIR:   %[[SECOND:.*]] = cir.get_global @_ZN6Holder6secondE : !cir.ptr<!rec_NeedsCtor>
// CIR:   cir.call @_ZN9NeedsCtorC1Ev(%[[SECOND]])
// CIR: cir.global external @_ZN6Holder5firstE = #cir.zero : !rec_NeedsCtor
// CIR: cir.func internal private @__cxx_global_var_init.1(){{.*}} {
// CIR:   %[[FIRST:.*]] = cir.get_global @_ZN6Holder5firstE : !cir.ptr<!rec_NeedsCtor>
// CIR:   cir.call @_ZN9NeedsCtorC1Ev(%[[FIRST]])

double get(double);

static const double a = get(2.0);
static const double b = 1.0 / a;
double c = b * 2.0;

__attribute__((weak)) double w = get(3.0);
double s = w + 1.0;

// CIR: cir.func {{.*}} @[[A_INIT:__cxx_global_var_init[.0-9]*]]()
// CIR-NEXT: %[[A:.+]] = cir.get_global @_ZL1a
// CIR:      cir.store {{.*}}, %[[A]]
// CIR: cir.func {{.*}} @[[C_INIT:__cxx_global_var_init[.0-9]*]]()
// CIR-NEXT: %[[C:.+]] = cir.get_global @c
// CIR:      cir.store {{.*}}, %[[C]]
// CIR: cir.func {{.*}} @[[W_INIT:__cxx_global_var_init[.0-9]*]]()
// CIR-NOT:  cir.func
// CIR:      %[[W:.+]] = cir.get_global @w
// CIR-NOT:  cir.func
// CIR:      cir.store {{.*}}, %[[W]]
// CIR: cir.func {{.*}} @[[S_INIT:__cxx_global_var_init[.0-9]*]]()
// CIR-NEXT: %[[S:.+]] = cir.get_global @s
// CIR:      cir.store {{.*}}, %[[S]]
// CIR: cir.func {{.*}} @[[B_INIT:__cxx_global_var_init[.0-9]*]]()
// CIR-NEXT: %[[B:.+]] = cir.get_global @_ZL1b
// CIR:      cir.store {{.*}}, %[[B]]

// CIR: cir.func internal private @_GLOBAL__sub_I_
// CIR:   cir.call @__cxx_global_var_init() : () -> ()
// CIR:   cir.call @__cxx_global_var_init.1() : () -> ()
// CIR-NEXT: cir.call @[[A_INIT]]()
// CIR-NEXT: cir.call @[[B_INIT]]()
// CIR-NEXT: cir.call @[[C_INIT]]()
// CIR-NEXT: cir.call @[[W_INIT]]()
// CIR-NEXT: cir.call @[[S_INIT]]()

// LLVM-DAG: @p = global ptr @_ZN6Holder5firstE, align 8
// LLVM-DAG: @_ZN6Holder6secondE = global %struct.NeedsCtor zeroinitializer, align 1
// LLVM-DAG: @_ZN6Holder5firstE = global %struct.NeedsCtor zeroinitializer, align 1

// LLVM: define internal void @__cxx_global_var_init()
// LLVM:   call void @_ZN9NeedsCtorC1Ev(ptr {{.*}} @_ZN6Holder6secondE)

// LLVM: define internal void @__cxx_global_var_init.1()
// LLVM:   call void @_ZN9NeedsCtorC1Ev(ptr {{.*}} @_ZN6Holder5firstE)

// LLVM: define internal void @[[A_INIT:__cxx_global_var_init[.0-9]*]]()
// LLVM:   store double %{{.+}}, ptr @_ZL1a
// LLVM: define internal void @[[C_INIT:__cxx_global_var_init[.0-9]*]]()
// LLVM:   store double %{{.+}}, ptr @c
// LLVM: define internal void @[[W_INIT:__cxx_global_var_init[.0-9]*]]()
// LLVM:   load i8, ptr @_ZGV1w
// LLVM:   store double %{{.+}}, ptr @w
// LLVM: define internal void @[[S_INIT:__cxx_global_var_init[.0-9]*]]()
// LLVM:   store double %{{.+}}, ptr @s
// LLVM: define internal void @[[B_INIT:__cxx_global_var_init[.0-9]*]]()
// LLVM:   store double %{{.+}}, ptr @_ZL1b

// LLVM: define internal void @_GLOBAL__sub_I_
// LLVM:   call void @__cxx_global_var_init()
// LLVM:   call void @__cxx_global_var_init.1()
// LLVM-NEXT: call void @[[A_INIT]]()
// LLVM-NEXT: call void @[[B_INIT]]()
// LLVM-NEXT: call void @[[C_INIT]]()
// LLVM-NEXT: call void @[[W_INIT]]()
// LLVM-NEXT: call void @[[S_INIT]]()
