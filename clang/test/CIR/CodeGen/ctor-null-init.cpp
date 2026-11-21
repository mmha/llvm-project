// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

struct A {
  A() = default;
  A(int); // This constructor triggers the null base class initialization.
};

struct B : A {
};

void test_empty_base_null_init() {
  B{};
}

// CIR: cir.func {{.*}} @_Z25test_empty_base_null_initv()
// CIR-NEXT:   %[[B_ADDR:.*]] = cir.alloca "agg.tmp.ensured" {{.*}} : !cir.ptr<!rec_B>
// CIR-NEXT:   %[[A_ADDR:.*]] = cir.base_class_addr nonnull %[[B_ADDR]] [0] : !cir.ptr<!rec_B> -> !cir.ptr<!rec_A>

// LLVM: define{{.*}} @_Z25test_empty_base_null_initv()
// LLVM-NEXT:   %[[B:.*]] = alloca %struct.B
// LLVM-NEXT:   ret void

// OGCG: define{{.*}} @_Z25test_empty_base_null_initv()
// OGCG-NEXT: entry:
// OGCG-NEXT:   %[[B:.*]] = alloca %struct.B
// OGCG-NEXT:   ret void


struct C {
  int c;
  C() = default;
  C(int); // This constructor triggers the null base class initialization.
};

struct D : C {
};

void test_non_empty_base_null_init() {
  D{};
}

// CIR: cir.func {{.*}} @_Z29test_non_empty_base_null_initv()
// CIR:   %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" {{.*}} : !cir.ptr<!rec_D>
// CIR:   %[[BASE:.*]] = cir.base_class_addr nonnull %[[TMP]] [0] : !cir.ptr<!rec_D> -> !cir.ptr<!rec_C>
// CIR:   %[[VOID:.*]] = cir.cast bitcast %[[BASE]] : !cir.ptr<!rec_C> -> !cir.ptr<!void>
// CIR:   cir.libc.memset %{{.+}} bytes at %[[VOID]] align(4) to %{{.+}}

// LLVM: define{{.*}} void @_Z29test_non_empty_base_null_initv()
// LLVM:   %[[TMP:.*]] = alloca %struct.D
// LLVM:   call void @llvm.memset.p0.i64(ptr {{.*}}%[[TMP]], i8 0, i64 4, i1 false)

// OGCG: define {{.*}} void @_Z29test_non_empty_base_null_initv()
// OGCG:   %[[TMP:.*]] = alloca %struct.D
// OGCG:   %[[BASE:.*]] = getelementptr inbounds i8, ptr %[[TMP]], i64 0
// OGCG:   call void @llvm.memset.p0.i64(ptr{{.*}} %[[BASE]], i8 0, i64 4, i1 false)

struct E {
  int e;
};

struct F : E {
  F() = default;
  F(int);
};

struct G : F {
};

void test_base_chain_null_init() {
  G{};
}

// CIR: cir.func {{.*}} @_Z25test_base_chain_null_initv()
// CIR:   %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" {{.*}} : !cir.ptr<!rec_G>
// CIR:   %[[BASE:.*]] = cir.base_class_addr nonnull %[[TMP]] [0] : !cir.ptr<!rec_G> -> !cir.ptr<!rec_F>
// CIR:   %[[VOID:.*]] = cir.cast bitcast %[[BASE]] : !cir.ptr<!rec_F> -> !cir.ptr<!void>
// CIR:   cir.libc.memset %{{.+}} bytes at %[[VOID]] align(4) to %{{.+}}

// LLVM: define{{.*}} void @_Z25test_base_chain_null_initv()
// LLVM:   %[[TMP:.*]] = alloca %struct.G
// LLVM:   call void @llvm.memset.p0.i64(ptr {{.*}}%[[TMP]], i8 0, i64 4, i1 false)

// OGCG: define {{.*}} void @_Z25test_base_chain_null_initv()
// OGCG:   %[[TMP:.*]] = alloca %struct.G
// OGCG:   %[[BASE:.*]] = getelementptr inbounds i8, ptr %[[TMP]], i64 0
// OGCG:   call void @llvm.memset.p0.i64(ptr{{.*}} %[[BASE]], i8 0, i64 4, i1 false)

struct H {
  int h;
  void *p;
  H() = default;
  H(int);
};

struct I : H {
};

void test_padded_base_null_init() {
  I{};
}

// CIR: cir.func {{.*}} @_Z26test_padded_base_null_initv()
// CIR:   %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" {{.*}} : !cir.ptr<!rec_I>
// CIR:   %[[BASE:.*]] = cir.base_class_addr nonnull %[[TMP]] [0] : !cir.ptr<!rec_I> -> !cir.ptr<!rec_H>
// CIR:   %[[VOID:.*]] = cir.cast bitcast %[[BASE]] : !cir.ptr<!rec_H> -> !cir.ptr<!void>
// CIR:   %[[SIZE:.*]] = cir.const #cir.int<16> : !u64i
// CIR:   cir.libc.memset %[[SIZE]] bytes at %[[VOID]] align(8) to %{{.+}}
// CIR-NOT:   cir.store
// CIR:   cir.return

// LLVM: define{{.*}} void @_Z26test_padded_base_null_initv()
// LLVM:   %[[TMP:.*]] = alloca %struct.I
// LLVM:   call void @llvm.memset.p0.i64(ptr {{.*}}%[[TMP]], i8 0, i64 16, i1 false)
// LLVM-NOT:   store
// LLVM:   ret void

// OGCG: define {{.*}} void @_Z26test_padded_base_null_initv()
// OGCG:   %[[TMP:.*]] = alloca %struct.I
// OGCG:   %[[BASE:.*]] = getelementptr inbounds i8, ptr %[[TMP]], i64 0
// OGCG:   call void @llvm.memset.p0.i64(ptr{{.*}} %[[BASE]], i8 0, i64 16, i1 false)

struct VBase {
  virtual ~VBase();
};

struct VDerived : VBase {
  VDerived();
};
VDerived::VDerived() : VBase() {}

// OGCG-LABEL: define {{.*}}@_ZN8VDerivedC2Ev
// OGCG: %[[ADDR:.*]] = getelementptr inbounds i8, ptr %[[THIS:.*]],
// OGCG: call void @llvm.memset.p0.i64(ptr align 8 %[[ADDR]], i8 0, i64 8, i1 false)
// OGCG: call void @_ZN5VBaseC2Ev(ptr noundef nonnull align 8 dereferenceable(8) %[[THIS]])
// OGCG: store ptr getelementptr inbounds inrange(-16, 16) (i8, ptr @_ZTV8VDerived, i64 16), ptr %[[THIS]], align 8

// CIR-LABEL: cir.func {{.*}}@_ZN8VDerivedC2Ev
// CIR: %[[BASE:.*]] = cir.base_class_addr nonnull {{.*}} [0] : !cir.ptr<!rec_VDerived> -> !cir.ptr<!rec_VBase>
// CIR: %[[VOID:.*]] = cir.cast bitcast %[[BASE]] : !cir.ptr<!rec_VBase> -> !cir.ptr<!void>
// CIR: cir.libc.memset %{{.+}} bytes at %[[VOID]] align(8) to %{{.+}}
// CIR: cir.call @_ZN5VBaseC2Ev(%[[BASE]])
// CIR: cir.vtable.address_point(@_ZTV8VDerived
// CIR:  %{{.+}} = cir.vtable.get_vptr

// LLVM-LABEL: define {{.*}}@_ZN8VDerivedC2Ev
// LLVM: call void @llvm.memset.p0.i64(ptr {{.*}}%[[ADDR:.*]], i8 0, i64 8, i1 false)
// LLVM: call void @_ZN5VBaseC2Ev(ptr {{.*}}%[[ADDR]])
// LLVM: store ptr getelementptr inbounds nuw (i8, ptr @_ZTV8VDerived, i64 16), ptr %[[ADDR]]

// CIR-LABEL: cir.func {{.*}}@_ZN5VBaseC2Ev
// CIR: cir.vtable.address_point(@_ZTV5VBase
// CIR: cir.vtable.get_vptr

// LLVM-LABEL: define {{.*}}@_ZN5VBaseC2Ev
// LLVM: store ptr getelementptr inbounds nuw (i8, ptr @_ZTV5VBase, i64 16), ptr %
// OGCG-LABEL: define {{.*}}@_ZN5VBaseC2Ev
// OGCG: store ptr getelementptr inbounds inrange(-16, 16) (i8, ptr @_ZTV5VBase, i64 16), ptr %
