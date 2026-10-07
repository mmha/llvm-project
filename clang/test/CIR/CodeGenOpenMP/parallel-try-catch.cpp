// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: cir-opt -cir-hoist-allocas -cir-flatten-cfg -cir-eh-abi-lowering %t.cir -o %t.eh.cir
// RUN: FileCheck --input-file=%t.eh.cir %s --check-prefix=CIR-EH --implicit-check-not='!cir.eh_token'
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -fclangir -emit-llvm -DNO_TYPED_CATCH %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPED-ERR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// The landing pads of a try/catch inside 'omp parallel' are in the region of
// the omp.parallel op, not in the function body. The EH ABI lowering must
// lower the !cir.eh_token block arguments there and put the terminate landing
// pad of a throwing catch-parameter copy into that region.
//
// The CIR to LLVM IR run leaves out the typed catch: OpenMPIRBuilder outlines
// the parallel region with CodeExtractor, which does not extract blocks that
// call llvm.eh.typeid.for (llvm.org/PR39545), and would fail an assertion
// ("Expected OpenMP outlining to be possible!"). The lowering to the LLVM
// dialect reports an error instead.
//
// FIXME: CIRGen does not yet wrap OpenMP structured blocks in the terminate
// scope that classic CodeGen pushes (CGOpenMPRegionInfo::EmitBody). Where
// classic CodeGen adds 'catch ptr null' to the landing pad, calls
// __clang_call_terminate for an unmatched exception and invokes
// __cxa_end_catch with a terminate landing pad, CIR resumes the exception out
// of the omp.parallel region and calls __cxa_end_catch.

void may_throw();

struct E {
  E();
  E(const E &);
  ~E();
};

void catch_all_in_parallel() {
#pragma omp parallel
  {
    try {
      may_throw();
    } catch (...) {
    }
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z21catch_all_in_parallelv()
// CIR:         omp.parallel {
// CIR:           cir.try {
// CIR:             cir.call @_Z9may_throwv()
// CIR:           } catch all (%{{.*}}: !cir.eh_token {{.*}}) {
// CIR:           omp.terminator

// CIR-EH-LABEL: cir.func {{.*}} @_Z21catch_all_in_parallelv()
// CIR-EH:         omp.parallel {
// CIR-EH:           cir.try_call @_Z9may_throwv() ^{{.*}}, ^[[LPAD:bb[0-9]+]]
// CIR-EH:         ^[[LPAD]]:
// CIR-EH:           %[[EXN:.*]], %[[TID:.*]] = cir.eh.inflight_exception catch_all
// CIR-EH:           cir.br ^[[DISPATCH:bb[0-9]+]](%[[EXN]], %[[TID]] : !cir.ptr<!void>, !u32i)
// CIR-EH:         ^[[DISPATCH]](%[[D_EXN:.*]]: !cir.ptr<!void>, %[[D_TID:.*]]: !u32i):
// CIR-EH:           cir.br ^[[CATCH:bb[0-9]+]](%[[D_EXN]], %[[D_TID]] : !cir.ptr<!void>, !u32i)
// CIR-EH:         ^[[CATCH]](%[[C_EXN:.*]]: !cir.ptr<!void>, %{{.*}}: !u32i):
// CIR-EH:           cir.call @__cxa_begin_catch(%[[C_EXN]])
// CIR-EH:           cir.call @__cxa_end_catch()
// CIR-EH:           omp.terminator
// CIR-EH-NEXT:    }
// CIR-EH-NEXT:    cir.return

// LLVM-LABEL: define dso_local void @_Z21catch_all_in_parallelv()
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 0, ptr @_Z21catch_all_in_parallelv..omp_par)

// LLVM-LABEL: define internal void @_Z21catch_all_in_parallelv..omp_par(
// LLVM-SAME:    personality ptr @__gxx_personality_v0
// LLVM:         invoke void @_Z9may_throwv()
// LLVM-NEXT:            to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM:       [[LPAD]]:
// LLVM-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:            catch ptr null
// LLVM:         call ptr @__cxa_begin_catch(ptr %{{.*}})
// FIXME: Classic CodeGen invokes __cxa_end_catch with a terminate landing pad.
// LLVM:         call void @__cxa_end_catch()

// OGCG-LABEL: define dso_local void @_Z21catch_all_in_parallelv()
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 0, ptr @_Z21catch_all_in_parallelv.omp_outlined)

// OGCG-LABEL: define internal void @_Z21catch_all_in_parallelv.omp_outlined(
// OGCG-SAME:    personality ptr @__gxx_personality_v0
// OGCG:         invoke void @_Z9may_throwv()
// OGCG-NEXT:            to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG:       [[LPAD]]:
// OGCG-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// OGCG-NEXT:            catch ptr null
// OGCG:         call ptr @__cxa_begin_catch(ptr %{{.*}})
// OGCG:         invoke void @__cxa_end_catch()

// TYPED-ERR: parallel-try-catch.cpp:[[#@LINE+5]]:5: error: catching a specific exception type inside an OpenMP parallel region is not yet implemented
#ifndef NO_TYPED_CATCH
void catch_by_copy_in_parallel() {
#pragma omp parallel
  {
    try {
      may_throw();
    } catch (E e) {
    }
  }
}
#endif

// CIR-LABEL: cir.func {{.*}} @_Z25catch_by_copy_in_parallelv()
// CIR:         omp.parallel {
// CIR:           %[[E:.*]] = cir.alloca "e"
// CIR:           cir.try {
// CIR:             cir.call @_Z9may_throwv()
// CIR:           } catch [type #cir.global_view<@_ZTI1E> : !cir.ptr<!u8i>] (%[[TOK:.*]]: !cir.eh_token {{.*}}) {
// CIR:             cir.construct_catch_param non_trivial_copy %[[TOK]] to %[[E]] using @__clang_cir_catch_copy__ZTS1E
// CIR:           } unwind (%[[UTOK:.*]]: !cir.eh_token {{.*}}) {
// CIR:             cir.resume %[[UTOK]] : !cir.eh_token
// CIR:           omp.terminator

// The copy constructor can throw, so it unwinds to a terminate landing pad,
// which must be in the omp.parallel region with the call.
// CIR-EH-LABEL: cir.func {{.*}} @_Z25catch_by_copy_in_parallelv()
// CIR-EH:         omp.parallel {
// CIR-EH:           %[[E:.*]] = cir.alloca "e"
// CIR-EH:           cir.try_call @_Z9may_throwv() ^{{.*}}, ^[[LPAD:bb[0-9]+]]
// CIR-EH:         ^[[LPAD]]:
// CIR-EH:           %[[EXN:.*]], %[[TID:.*]] = cir.eh.inflight_exception [@_ZTI1E]
// CIR-EH:           cir.br ^[[DISPATCH:bb[0-9]+]](%[[EXN]], %[[TID]] : !cir.ptr<!void>, !u32i)
// CIR-EH:         ^[[DISPATCH]](%[[D_EXN:.*]]: !cir.ptr<!void>, %[[D_TID:.*]]: !u32i):
// CIR-EH:           cir.br ^[[CMP:bb[0-9]+]](%[[D_EXN]], %[[D_TID]] : !cir.ptr<!void>, !u32i)
// CIR-EH:         ^[[CMP]](%[[CMP_EXN:.*]]: !cir.ptr<!void>, %[[CMP_TID:.*]]: !u32i):
// CIR-EH:           %[[TYPEID:.*]] = cir.eh.typeid @_ZTI1E
// CIR-EH:           %[[MATCH:.*]] = cir.cmp eq %[[CMP_TID]], %[[TYPEID]] : !u32i
// CIR-EH:           cir.brcond %[[MATCH]] ^[[CATCH:bb[0-9]+]](%[[CMP_EXN]], %[[CMP_TID]] : !cir.ptr<!void>, !u32i), ^[[RESUME:bb[0-9]+]](%[[CMP_EXN]], %[[CMP_TID]] : !cir.ptr<!void>, !u32i)
// CIR-EH:         ^[[CATCH]](%[[C_EXN:.*]]: !cir.ptr<!void>, %{{.*}}: !u32i):
// CIR-EH:           %[[EXN_OBJ:.*]] = cir.call @__cxa_get_exception_ptr(%[[C_EXN]]) nothrow
// CIR-EH:           %[[EXN_E:.*]] = cir.cast bitcast %[[EXN_OBJ]] : !cir.ptr<!u8i> -> !cir.ptr<!rec_E>
// CIR-EH:           cir.try_call @_ZN1EC1ERKS_(%[[E]], %[[EXN_E]]) ^[[COPIED:bb[0-9]+]], ^[[TERMINATE:bb[0-9]+]]
// CIR-EH:         ^[[COPIED]]:
// CIR-EH:           cir.call @__cxa_begin_catch(%[[C_EXN]])
// CIR-EH:           cir.call @_ZN1ED1Ev(%[[E]]) nothrow
// CIR-EH:           cir.call @__cxa_end_catch()
// FIXME: Classic CodeGen calls __clang_call_terminate for an unmatched
// exception instead of resuming it out of the parallel region.
// CIR-EH:         ^[[RESUME]](%[[R_EXN:.*]]: !cir.ptr<!void>, %[[R_TID:.*]]: !u32i):
// CIR-EH:           cir.resume.flat %[[R_EXN]], %[[R_TID]]
// CIR-EH:           omp.terminator
// CIR-EH:         ^[[TERMINATE]]:
// CIR-EH-NEXT:      %[[T_EXN:.*]], %{{.*}} = cir.eh.inflight_exception catch_all
// CIR-EH-NEXT:      cir.call @__clang_call_terminate(%[[T_EXN]]) nothrow {noreturn}
// CIR-EH-NEXT:      cir.unreachable
// CIR-EH-NEXT:    }
// CIR-EH-NEXT:    cir.return

// OGCG-LABEL: define internal void @_Z25catch_by_copy_in_parallelv.omp_outlined(
// OGCG-SAME:    personality ptr @__gxx_personality_v0
// OGCG:         invoke void @_Z9may_throwv()
// OGCG:         landingpad { ptr, i32 }
// OGCG-NEXT:            catch ptr @_ZTI1E
// OGCG-NEXT:            catch ptr null
// OGCG:         call ptr @__cxa_get_exception_ptr(ptr %{{.*}})
// OGCG:         invoke void @_ZN1EC1ERKS_(ptr {{.*}} %e, ptr {{.*}})
// OGCG-NEXT:            to label %{{.*}} unwind label %[[TERMINATE:.*]]
// OGCG:       [[TERMINATE]]:
// OGCG-NEXT:    %[[T_LP:.*]] = landingpad { ptr, i32 }
// OGCG-NEXT:            catch ptr null
// OGCG-NEXT:    %[[T_EXN:.*]] = extractvalue { ptr, i32 } %[[T_LP]], 0
// OGCG-NEXT:    call void @__clang_call_terminate(ptr %[[T_EXN]])
