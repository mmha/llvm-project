// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fno-optimize-sibling-calls %s -o - | FileCheck %s -check-prefix=ALL-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fno-optimize-sibling-calls %s -o %t-all-cir.ll
// RUN: FileCheck --input-file=%t-all-cir.ll %s -check-prefix=ALL
// RUN: FileCheck --input-file=%t-all-cir.ll %s -check-prefix=ALL-INIT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fno-optimize-sibling-calls %s -o %t-all.ll
// RUN: FileCheck --input-file=%t-all.ll %s -check-prefix=ALL
// RUN: FileCheck --input-file=%t-all.ll %s -check-prefix=ALL-INIT

int g(int);

__attribute__((disable_tail_calls)) int no_tail(int x) { return g(x); }
int may_tail(int x) { return g(x); }

struct T {
  T();
  ~T();
};
T t;

// CIR: cir.func {{.*}} @_Z7no_taili({{.*}} attributes {{{.*}}disable_tail_calls
// CIR: cir.func {{.*}} @_Z8may_taili(
// CIR-NOT: {{[ ,{]}}disable_tail_calls{{[,}]}}

// LLVM: define {{.*}} i32 @_Z7no_taili({{.*}}) #[[NO:[0-9]+]]
// LLVM: define {{.*}} i32 @_Z8may_taili({{.*}}) #[[MAY:[0-9]+]]
// LLVM: attributes #[[NO]] = {{{.*}}"disable-tail-calls"="true"
// LLVM-NOT: attributes #[[MAY]] = {{{.*}}"disable-tail-calls"

// OGCG: define {{.*}} i32 @_Z7no_taili({{.*}}) #[[NO:[0-9]+]]
// OGCG: define {{.*}} i32 @_Z8may_taili({{.*}}) #[[MAY:[0-9]+]]
// OGCG: attributes #[[NO]] = {{{.*}}"disable-tail-calls"="true"
// OGCG-NOT: attributes #[[MAY]] = {{{.*}}"disable-tail-calls"

// ALL-CIR: module {{.*}}cir.lowering_options = #cir.lowering_options<{{.*}}disable_tail_calls = true
// ALL-CIR: cir.func {{.*}} @_Z8may_taili(
// ALL-CIR-NOT: {{[ ,{]}}disable_tail_calls{{[,}]}}

// ALL: define {{.*}} i32 @_Z8may_taili({{.*}}) #[[MAY:[0-9]+]]
// ALL: define internal void @_GLOBAL__sub_I_disable_tail_calls.cpp() #[[SUB_ATTRS:[0-9]+]]
// ALL-DAG: attributes #[[MAY]] = {{{.*}}"disable-tail-calls"="true"
// ALL-DAG: attributes #[[SUB_ATTRS]] = {{{.*}}"disable-tail-calls"="true"

// ALL-INIT: define internal void @[[INIT:__cxx_global_var_init[.0-9]*]]() #[[INIT_ATTRS:[0-9]+]]
// ALL-INIT-NOT: define
// ALL-INIT:   call {{.*}}@__cxa_atexit(
// ALL-INIT: define internal void @_GLOBAL__sub_I_disable_tail_calls.cpp()
// ALL-INIT:   call void @[[INIT]]()
// ALL-INIT: attributes #[[INIT_ATTRS]] = {{{.*}}"disable-tail-calls"="true"
