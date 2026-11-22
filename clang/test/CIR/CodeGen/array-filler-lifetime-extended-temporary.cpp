// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

struct T {
  T(int);
  ~T();
};
struct R {
  const T &r = T(4);
  int x;
};
void use(R *);

void three() {
  R a[3] = {};
  use(a);
}

// CIR-LABEL: cir.func {{.*}} @_Z5threev()
// CIR:         %[[T0:.+]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T1:.+]] = cir.alloca "ref.tmp1" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T2:.+]] = cir.alloca "ref.tmp2" {{.*}} : !cir.ptr<!rec_T>
// CIR-NOT:     cir.do
// CIR:         cir.call @_ZN1TC1Ei(%[[T0]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T1]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T2]], {{.*}})
// CIR:         cir.call @_Z3useP1R(
// CIR:         cir.call @_ZN1TD1Ev(%[[T2]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T1]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T0]])

// LLVM-LABEL: define {{.*}} void @_Z5threev()
// LLVM:         %[[T0:.+]] = alloca %struct.T
// LLVM:         %[[T1:.+]] = alloca %struct.T
// LLVM:         %[[T2:.+]] = alloca %struct.T
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T0]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T1]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T2]], i32 noundef 4)
// LLVM:         call void @_Z3useP1R(
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T2]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T1]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T0]])

// OGCG-LABEL: define {{.*}} void @_Z5threev()
// OGCG:         call void @_ZN1TC1Ei(ptr
// OGCG:         call void @_Z3useP1R(

void two_by_two() {
  R b[2][2] = {};
  use(b[0]);
}

struct Inner2 {
  R inner[2];
};
void nested_record() {
  Inner2 c[2] = {};
  use(c[0].inner);
}

// CIR-LABEL: cir.func {{.*}} @_Z10two_by_twov()
// CIR:         %[[T0:.+]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T1:.+]] = cir.alloca "ref.tmp1" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T2:.+]] = cir.alloca "ref.tmp2" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T3:.+]] = cir.alloca "ref.tmp3" {{.*}} : !cir.ptr<!rec_T>
// CIR-NOT:     cir.do
// CIR:         cir.call @_ZN1TC1Ei(%[[T0]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T1]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T2]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T3]], {{.*}})
// CIR:         cir.call @_Z3useP1R(
// CIR:         cir.call @_ZN1TD1Ev(%[[T3]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T2]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T1]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T0]])

// CIR-LABEL: cir.func {{.*}} @_Z13nested_recordv()
// CIR:         %[[T0:.+]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T1:.+]] = cir.alloca "ref.tmp1" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T2:.+]] = cir.alloca "ref.tmp2" {{.*}} : !cir.ptr<!rec_T>
// CIR:         %[[T3:.+]] = cir.alloca "ref.tmp3" {{.*}} : !cir.ptr<!rec_T>
// CIR-NOT:     cir.do
// CIR:         cir.call @_ZN1TC1Ei(%[[T0]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T1]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T2]], {{.*}})
// CIR:         cir.call @_ZN1TC1Ei(%[[T3]], {{.*}})
// CIR:         cir.call @_Z3useP1R(
// CIR:         cir.call @_ZN1TD1Ev(%[[T3]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T2]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T1]])
// CIR:         cir.call @_ZN1TD1Ev(%[[T0]])

// LLVM-LABEL: define {{.*}} void @_Z10two_by_twov()
// LLVM:         %[[T0:.+]] = alloca %struct.T
// LLVM:         %[[T1:.+]] = alloca %struct.T
// LLVM:         %[[T2:.+]] = alloca %struct.T
// LLVM:         %[[T3:.+]] = alloca %struct.T
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T0]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T1]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T2]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T3]], i32 noundef 4)
// LLVM:         call void @_Z3useP1R(
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T3]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T2]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T1]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T0]])

// LLVM-LABEL: define {{.*}} void @_Z13nested_recordv()
// LLVM:         %[[T0:.+]] = alloca %struct.T
// LLVM:         %[[T1:.+]] = alloca %struct.T
// LLVM:         %[[T2:.+]] = alloca %struct.T
// LLVM:         %[[T3:.+]] = alloca %struct.T
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T0]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T1]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T2]], i32 noundef 4)
// LLVM:         call void @_ZN1TC1Ei(ptr {{.*}}%[[T3]], i32 noundef 4)
// LLVM:         call void @_Z3useP1R(
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T3]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T2]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T1]])
// LLVM:         call void @_ZN1TD1Ev(ptr {{.*}}%[[T0]])

int next();
struct L {
  int x = [] {
    const int &local = next();
    return local;
  }();
};
void useL(L *);
void lambda_local() {
  L d[257] = {};
  useL(d);
}

// CIR-LABEL: cir.func {{.*}} @_Z12lambda_localv()
// CIR:         cir.do {
// CIR:           cir.call @_ZNK1L1xMUlvE_clEv(
// CIR:         } while {
// CIR:         cir.call @_Z4useLP1L(

// LLVM-LABEL: define {{.*}} void @_Z12lambda_localv()
// LLVM:         call {{.*}} @_ZNK1L1xMUlvE_clEv(
// LLVM-NOT:     call {{.*}} @_ZNK1L1xMUlvE_clEv(
// LLVM:         call void @_Z4useLP1L(

void at_limit() {
  R e[256] = {};
  use(e);
}

// LLVM-LABEL: define {{.*}} void @_Z8at_limitv()
// LLVM-COUNT-256: call void @_ZN1TC1Ei(
// LLVM-NOT:     call void @_ZN1TC1Ei(
// LLVM:         call void @_Z3useP1R(
// LLVM-COUNT-256: call void @_ZN1TD1Ev(
