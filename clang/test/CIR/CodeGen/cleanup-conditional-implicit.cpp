// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// A temporary that is created conditionally in a default argument, a default
// member initializer or a lambda's capture initializer is destroyed at the end
// of the enclosing full-expression, guarded by its active flag.

bool cond();

struct T {
  T();
  ~T();
  bool ok();
};

void f(bool = cond() && T().ok());

void default_argument() { f(); }

// CIR-LABEL: cir.func {{.*}} @_Z16default_argumentv(
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.cond"
// CIR:         cir.cleanup.scope {
// CIR:           cir.call @_ZN1TC1Ev(%[[TMP]])
// CIR:           cir.call @_Z1fb(
// CIR:         } cleanup normal {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN1TD1Ev(%[[TMP]])

// LLVM-LABEL: define {{.*}} void @_Z16default_argumentv(
// LLVM:         %[[TMP:.*]] = alloca %struct.T
// LLVM:         %[[ACTIVE:.*]] = alloca i8
// LLVM:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP]])
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE]]
// LLVM:         call void @_Z1fb(
// LLVM:         %[[A:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[B:.*]] = trunc i8 %[[A]] to i1
// LLVM-NEXT:    br i1 %[[B]], label %[[DTOR:.*]], label
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])

// OGCG-LABEL: define {{.*}} void @_Z16default_argumentv(
// OGCG:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG:         call void @_Z1fb(
// OGCG:         br i1 %cleanup.is_active, label %cleanup.action
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])

struct D {
  D(bool = cond() && T().ok());
};

void default_ctor_argument() { D d; }

// CIR-LABEL: cir.func {{.*}} @_Z21default_ctor_argumentv(
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.cond"
// CIR:         cir.cleanup.scope {
// CIR:           cir.call @_ZN1TC1Ev(%[[TMP]])
// CIR:           cir.call @_ZN1DC1Eb(
// CIR:         } cleanup normal {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN1TD1Ev(%[[TMP]])

// LLVM-LABEL: define {{.*}} void @_Z21default_ctor_argumentv(
// LLVM:         %[[TMP:.*]] = alloca %struct.T
// LLVM:         %[[ACTIVE:.*]] = alloca i8
// LLVM:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP]])
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE]]
// LLVM:         call void @_ZN1DC1Eb(
// LLVM:         %[[A:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[B:.*]] = trunc i8 %[[A]] to i1
// LLVM-NEXT:    br i1 %[[B]], label %[[DTOR:.*]], label
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])

// OGCG-LABEL: define {{.*}} void @_Z21default_ctor_argumentv(
// OGCG:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG:         call void @_ZN1DC1Eb(
// OGCG:         br i1 %cleanup.is_active, label %cleanup.action
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])

struct S {
  int i;
  bool b = cond() && T().ok();
};

void use(S);

void aggregate_default_member_init() {
  S s{1};
  use(s);
}

// CIR-LABEL: cir.func {{.*}} @_Z29aggregate_default_member_initv(
// CIR:         %[[S:.*]] = cir.alloca "s"
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.cond"
// CIR:         %[[B:.*]] = cir.get_member %[[S]][1] {name = "b"}
// CIR:         cir.cleanup.scope {
// CIR:           cir.call @_ZN1TC1Ev(%[[TMP]])
// CIR:           cir.store {{.*}}, %[[B]]
// CIR:         } cleanup normal {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN1TD1Ev(%[[TMP]])
// CIR:         cir.call @_Z3use1S(

// LLVM-LABEL: define {{.*}} void @_Z29aggregate_default_member_initv(
// LLVM:         %[[TMP:.*]] = alloca %struct.T
// LLVM:         %[[ACTIVE:.*]] = alloca i8
// LLVM:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP]])
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE]]
// LLVM:         %[[A:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[B:.*]] = trunc i8 %[[A]] to i1
// LLVM-NEXT:    br i1 %[[B]], label %[[DTOR:.*]], label
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// LLVM:         call void @_Z3use1S(

// OGCG-LABEL: define {{.*}} void @_Z29aggregate_default_member_initv(
// OGCG:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG:         br i1 %cleanup.is_active, label %cleanup.action
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// OGCG:         call void @_Z3use1S(

void paren_aggregate_default_member_init() {
  S s(1);
  use(s);
}

// CIR-LABEL: cir.func {{.*}} @_Z35paren_aggregate_default_member_initv(
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.cond"
// CIR:         cir.cleanup.scope {
// CIR:           cir.call @_ZN1TC1Ev(%[[TMP]])
// CIR:         } cleanup normal {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN1TD1Ev(%[[TMP]])
// CIR:         cir.call @_Z3use1S(

// LLVM-LABEL: define {{.*}} void @_Z35paren_aggregate_default_member_initv(
// LLVM:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// LLVM:         br i1 %{{.*}}, label %[[DTOR:.*]], label
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// LLVM:         call void @_Z3use1S(

// OGCG-LABEL: define {{.*}} void @_Z35paren_aggregate_default_member_initv(
// OGCG:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG:         br i1 %cleanup.is_active, label %cleanup.action
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// OGCG:         call void @_Z3use1S(

void use(bool);

void lambda_init_capture() {
  auto l = [b = cond() && T().ok()] { use(b); };
  l();
}

// CIR-LABEL: cir.func {{.*}} @_Z19lambda_init_capturev(
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.cond"
// CIR:         cir.cleanup.scope {
// CIR:           cir.call @_ZN1TC1Ev(%[[TMP]])
// CIR:         } cleanup normal {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN1TD1Ev(%[[TMP]])
// CIR:         cir.call @_ZZ19lambda_init_capturevENK3$_0clEv(

// LLVM-LABEL: define {{.*}} void @_Z19lambda_init_capturev(
// LLVM:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// LLVM:         br i1 %{{.*}}, label %[[DTOR:.*]], label
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// LLVM:         call void @"_ZZ19lambda_init_capturevENK3$_0clEv"(

// OGCG-LABEL: define {{.*}} void @_Z19lambda_init_capturev(
// OGCG:         call void @_ZN1TC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG:         br i1 %cleanup.is_active, label %cleanup.action
// OGCG:       cleanup.action:
// OGCG-NEXT:    call void @_ZN1TD1Ev(ptr {{.*}} %[[TMP]])
// OGCG:         call void @"_ZZ19lambda_init_capturevENK3$_0clEv"(
