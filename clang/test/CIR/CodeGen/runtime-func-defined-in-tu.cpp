// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -mmlir --mlir-print-ir-after=cir-eh-abi-lowering %s -o /dev/null 2> %t.eh.cir
// RUN: FileCheck --input-file=%t.eh.cir %s -check-prefix=CIR-EH
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

namespace __cxxabiv1 {
class __class_type_info;
}

extern "C" void *__cxa_begin_catch(void *exn) noexcept { return exn; }

void mayThrow();

void catchAll() {
  try {
    mayThrow();
  } catch (...) {
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z8catchAllv(
// CIR:         cir.begin_catch %{{.+}} : !cir.eh_token -> (!cir.catch_token, !cir.ptr<!void>)

// LLVM-LABEL: define {{.*}} void @_Z8catchAllv(
// LLVM:         call ptr @__cxa_begin_catch(ptr %{{.+}})

// OGCG-LABEL: define {{.*}} void @_Z8catchAllv(
// OGCG:         call ptr @__cxa_begin_catch(ptr %{{.+}})

struct A {
  virtual ~A();
};
struct B : A {};

B *downcast(A *a) { return dynamic_cast<B *>(a); }

// CIR-LABEL: cir.func {{.*}} @_Z8downcastP1A(
// CIR:         %[[FN:.+]] = cir.get_global @__dynamic_cast : !cir.ptr<!cir.func<(!cir.ptr<!void>, !cir.ptr<!rec___cxxabiv13A3A__class_type_info>, !cir.ptr<!rec___cxxabiv13A3A__class_type_info>, !s64i) -> !cir.ptr<!void>>>
// CIR:         %[[CAST:.+]] = cir.cast bitcast %[[FN]] : {{.*}} -> !cir.ptr<!cir.func<(!cir.ptr<!void>, !cir.ptr<!u8i>, !cir.ptr<!u8i>, !s64i) -> !cir.ptr<!void>>>
// CIR:         cir.call %[[CAST]](%{{.+}}, %{{.+}}, %{{.+}}, %{{.+}})

// LLVM-LABEL: define {{.*}} ptr @_Z8downcastP1A(
// LLVM:         call ptr @__dynamic_cast(ptr %{{.+}}, ptr @_ZTI1A, ptr @_ZTI1B, i64 0)

// OGCG-LABEL: define {{.*}} ptr @_Z8downcastP1A(
// OGCG:         call ptr @__dynamic_cast(ptr %{{.+}}, ptr @_ZTI1A, ptr @_ZTI1B, i64 0)

extern "C" void *__dynamic_cast(const void *sub,
                                const __cxxabiv1::__class_type_info *src,
                                const __cxxabiv1::__class_type_info *dst,
                                long src2dst) {
  return nullptr;
}

// CIR: cir.func {{.*}} @__dynamic_cast(%{{.+}}: !cir.ptr<!void> {{.*}}, %{{.+}}: !cir.ptr<!rec___cxxabiv13A3A__class_type_info> {{.*}}
// LLVM: define {{.*}} ptr @__dynamic_cast(
// OGCG: define {{.*}} ptr @__dynamic_cast(

extern "C" [[noreturn]] void __cxa_throw(void *obj,
                                         __cxxabiv1::__class_type_info *type,
                                         void (*dtor)(void *)) {
  __builtin_trap();
}

void throwInTry() {
  try {
    throw 1;
  } catch (...) {
  }
}

// CIR-EH-LABEL: cir.func {{.*}} @_Z10throwInTryv(
// CIR-EH:         %[[FN:.+]] = cir.get_global @__cxa_throw : !cir.ptr<!cir.func<(!cir.ptr<!void>, !cir.ptr<!rec___cxxabiv13A3A__class_type_info>, !cir.ptr<!cir.func<(!cir.ptr<!void>)>>)>>
// CIR-EH:         %[[CAST:.+]] = cir.cast bitcast %[[FN]] : {{.*}} -> !cir.ptr<!cir.func<(!cir.ptr<!void>, !cir.ptr<!void>, !cir.ptr<!void>)>>
// CIR-EH:         cir.try_call %[[CAST]](%{{.+}}, %{{.+}}, %{{.+}}) ^{{.+}}, ^[[UNWIND:.+]] :
// CIR-EH:       ^[[UNWIND]]:
// CIR-EH-NEXT:    %{{.+}}, %{{.+}} = cir.eh.inflight_exception catch_all

// LLVM-LABEL: define {{.*}} void @_Z10throwInTryv(
// LLVM:         invoke void @__cxa_throw(ptr %{{.+}}, ptr @_ZTIi, ptr null)
// LLVM-NEXT:      to label %{{.+}} unwind label %[[LPAD:.+]]
// LLVM:       {{^}}[[LPAD]]:
// LLVM-NEXT:    landingpad { ptr, i32 }
// LLVM-NEXT:      catch ptr null

// OGCG-LABEL: define {{.*}} void @_Z10throwInTryv(
// OGCG:         invoke void @__cxa_throw(ptr %{{.+}}, ptr @_ZTIi, ptr null)
// OGCG-NEXT:      to label %{{.+}} unwind label %[[LPAD:.+]]
// OGCG:       {{^}}[[LPAD]]:
// OGCG-NEXT:    landingpad { ptr, i32 }
// OGCG-NEXT:      catch ptr null
