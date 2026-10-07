// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: cir-opt -cir-flatten-cfg %t.cir -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s --check-prefix=CIR-FLAT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// A throw that unwinds to a catch or a cleanup inside 'omp parallel' becomes a
// cir.try_throw in the region of the omp.parallel op. Its normal destination,
// a block that holds only a cir.unreachable, must be in that region too.
//
// FIXME: CIRGen does not yet wrap OpenMP structured blocks in the terminate
// scope that classic CodeGen pushes (CGOpenMPRegionInfo::EmitBody). Where
// classic CodeGen invokes __cxa_end_catch with a terminate landing pad and
// calls __clang_call_terminate for an exception that would leave the region,
// CIR calls __cxa_end_catch and resumes the exception out of the outlined
// function.

struct S {
  ~S();
};

void throw_in_try_in_parallel(bool b) {
#pragma omp parallel
  {
    try {
      if (b)
        throw 1;
    } catch (...) {
    }
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z24throw_in_try_in_parallelb(
// CIR:         omp.parallel {
// CIR:           cir.try {
// CIR:             cir.if %{{.*}} {
// CIR:               cir.throw %{{.*}} : !cir.ptr<!s32i>, @_ZTIi
// CIR-NEXT:          cir.unreachable
// CIR:           } catch all (%{{.*}}: !cir.eh_token {{.*}}) {
// CIR:           omp.terminator

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z24throw_in_try_in_parallelb(
// CIR-FLAT:         omp.parallel {
// CIR-FLAT:           cir.try_throw %{{.*}} : !cir.ptr<!s32i>, @_ZTIi ^[[UNREACH:bb[0-9]+]], ^[[LPAD:bb[0-9]+]]
// CIR-FLAT-NEXT:    ^[[UNREACH]]:
// CIR-FLAT-NEXT:      cir.unreachable
// CIR-FLAT:         ^[[LPAD]]:
// CIR-FLAT-NEXT:      %[[TOK:.*]] = cir.eh.initiate : !cir.eh_token
// CIR-FLAT-NEXT:      cir.br ^[[DISPATCH:bb[0-9]+]](%[[TOK]] : !cir.eh_token)
// CIR-FLAT:         ^[[DISPATCH]](%[[D_TOK:.*]]: !cir.eh_token):
// CIR-FLAT-NEXT:      cir.eh.dispatch %[[D_TOK]] : !cir.eh_token [
// CIR-FLAT-NEXT:        catch_all : ^[[CATCH:bb[0-9]+]]
// CIR-FLAT:         ^[[CATCH]](%{{.*}}: !cir.eh_token):
// CIR-FLAT-NEXT:      cir.begin_catch
// CIR-FLAT:           cir.end_catch
// CIR-FLAT:           omp.terminator
// CIR-FLAT-NEXT:    }
// CIR-FLAT-NEXT:    cir.return

// LLVM-LABEL: define dso_local void @_Z24throw_in_try_in_parallelb(
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @_Z24throw_in_try_in_parallelb..omp_par, ptr %{{.*}})

// LLVM-LABEL: define internal void @_Z24throw_in_try_in_parallelb..omp_par(
// LLVM-SAME:    personality ptr @__gxx_personality_v0
// LLVM:         invoke void @__cxa_throw(ptr %{{.*}}, ptr @_ZTIi, ptr null)
// LLVM-NEXT:            to label %[[UNREACH:.*]] unwind label %[[LPAD:.*]]
// LLVM:       [[LPAD]]:
// LLVM-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:            catch ptr null
// LLVM:         call ptr @__cxa_begin_catch(ptr %{{.*}})
// FIXME: Classic CodeGen invokes __cxa_end_catch with a terminate landing pad.
// LLVM:         call void @__cxa_end_catch()
// LLVM:       [[UNREACH]]:
// LLVM-NEXT:    unreachable

// OGCG-LABEL: define dso_local void @_Z24throw_in_try_in_parallelb(
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @_Z24throw_in_try_in_parallelb.omp_outlined, ptr %{{.*}})

// OGCG-LABEL: define internal void @_Z24throw_in_try_in_parallelb.omp_outlined(
// OGCG-SAME:    personality ptr @__gxx_personality_v0
// OGCG:         invoke void @__cxa_throw(ptr %{{.*}}, ptr @_ZTIi, ptr null)
// OGCG-NEXT:            to label %[[UNREACH:.*]] unwind label %[[LPAD:.*]]
// OGCG:       [[LPAD]]:
// OGCG-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// OGCG-NEXT:            catch ptr null
// OGCG:         call ptr @__cxa_begin_catch(ptr %{{.*}})
// OGCG:         invoke void @__cxa_end_catch()
// OGCG:       [[UNREACH]]:
// OGCG-NEXT:    unreachable

void throw_in_cleanup_in_parallel(bool b) {
#pragma omp parallel
  {
    try {
      S s;
      if (b)
        throw 1;
    } catch (...) {
    }
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z28throw_in_cleanup_in_parallelb(
// CIR:         omp.parallel {
// CIR:           %[[S:.*]] = cir.alloca "s"
// CIR:           cir.try {
// CIR:             cir.cleanup.scope {
// CIR:               cir.if %{{.*}} {
// CIR:                 cir.throw %{{.*}} : !cir.ptr<!s32i>, @_ZTIi
// CIR-NEXT:            cir.unreachable
// CIR:             } cleanup all {
// CIR-NEXT:          cir.call @_ZN1SD1Ev(%[[S]])
// CIR:           } catch all (%{{.*}}: !cir.eh_token {{.*}}) {
// CIR:           omp.terminator

// The cleanup scope is flattened first: the throw unwinds to the destructor
// call, which then continues to the catch dispatch of the enclosing try.
// CIR-FLAT-LABEL: cir.func {{.*}} @_Z28throw_in_cleanup_in_parallelb(
// CIR-FLAT:         omp.parallel {
// CIR-FLAT:           %[[S:.*]] = cir.alloca "s"
// CIR-FLAT:           cir.try_throw %{{.*}} : !cir.ptr<!s32i>, @_ZTIi ^[[UNREACH:bb[0-9]+]], ^[[LPAD:bb[0-9]+]]
// CIR-FLAT-NEXT:    ^[[UNREACH]]:
// CIR-FLAT-NEXT:      cir.unreachable
// CIR-FLAT:         ^[[LPAD]]:
// CIR-FLAT-NEXT:      %[[TOK:.*]] = cir.eh.initiate : !cir.eh_token
// CIR-FLAT-NEXT:      cir.br ^[[CLEANUP:bb[0-9]+]](%[[TOK]] : !cir.eh_token)
// CIR-FLAT:         ^[[CLEANUP]](%[[C_TOK:.*]]: !cir.eh_token):
// CIR-FLAT-NEXT:      %[[CT:.*]] = cir.begin_cleanup %[[C_TOK]]
// CIR-FLAT-NEXT:      cir.call @_ZN1SD1Ev(%[[S]])
// CIR-FLAT-NEXT:      cir.end_cleanup %[[CT]]
// CIR-FLAT-NEXT:      cir.br ^[[DISPATCH:bb[0-9]+]](%[[C_TOK]] : !cir.eh_token)
// CIR-FLAT:         ^[[DISPATCH]](%[[D_TOK:.*]]: !cir.eh_token):
// CIR-FLAT-NEXT:      cir.eh.dispatch %[[D_TOK]] : !cir.eh_token [
// CIR-FLAT-NEXT:        catch_all : ^[[CATCH:bb[0-9]+]]
// CIR-FLAT:         ^[[CATCH]](%{{.*}}: !cir.eh_token):
// CIR-FLAT-NEXT:      cir.begin_catch
// CIR-FLAT:           cir.end_catch
// CIR-FLAT:           omp.terminator
// CIR-FLAT-NEXT:    }
// CIR-FLAT-NEXT:    cir.return

// LLVM-LABEL: define dso_local void @_Z28throw_in_cleanup_in_parallelb(
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @_Z28throw_in_cleanup_in_parallelb..omp_par, ptr %{{.*}})

// LLVM-LABEL: define internal void @_Z28throw_in_cleanup_in_parallelb..omp_par(
// LLVM-SAME:    personality ptr @__gxx_personality_v0
// LLVM:         %[[S:.*]] = alloca %struct.S
// LLVM:         invoke void @__cxa_throw(ptr %{{.*}}, ptr @_ZTIi, ptr null)
// LLVM-NEXT:            to label %[[UNREACH:.*]] unwind label %[[LPAD:.*]]
// LLVM:       [[LPAD]]:
// LLVM-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:            catch ptr null
// LLVM:         call void @_ZN1SD1Ev(ptr {{.*}} %[[S]])
// LLVM:         call ptr @__cxa_begin_catch(ptr %{{.*}})
// FIXME: Classic CodeGen invokes __cxa_end_catch with a terminate landing pad.
// LLVM:         call void @__cxa_end_catch()
// LLVM:       [[UNREACH]]:
// LLVM-NEXT:    unreachable

// OGCG-LABEL: define dso_local void @_Z28throw_in_cleanup_in_parallelb(
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @{{.*}}, i32 1, ptr @_Z28throw_in_cleanup_in_parallelb.omp_outlined, ptr %{{.*}})

// OGCG-LABEL: define internal void @_Z28throw_in_cleanup_in_parallelb.omp_outlined(
// OGCG-SAME:    personality ptr @__gxx_personality_v0
// OGCG:         %[[S:.*]] = alloca %struct.S
// OGCG:         invoke void @__cxa_throw(ptr %{{.*}}, ptr @_ZTIi, ptr null)
// OGCG-NEXT:            to label %[[UNREACH:.*]] unwind label %[[LPAD:.*]]
// OGCG:       [[LPAD]]:
// OGCG-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// OGCG-NEXT:            catch ptr null
// OGCG:         call void @_ZN1SD1Ev(ptr {{.*}} %[[S]])
// OGCG:         call ptr @__cxa_begin_catch(ptr %{{.*}})
// OGCG:         invoke void @__cxa_end_catch()
// OGCG:       [[UNREACH]]:
// OGCG-NEXT:    unreachable

// A throw that unwinds only to a cleanup: there is no try in the region, so
// the cir.try_throw unwinds to the destructor call, which then resumes.
void throw_in_cleanup_no_try_in_parallel(bool b) {
#pragma omp parallel
  {
    {
      S s;
      if (b)
        throw 1;
    }
  }
}

// CIR-LABEL: cir.func {{.*}} @_Z35throw_in_cleanup_no_try_in_parallelb(
// CIR:         omp.parallel {
// CIR:           %[[S:.*]] = cir.alloca "s"
// CIR:           cir.cleanup.scope {
// CIR:             cir.if %{{.*}} {
// CIR:               cir.throw %{{.*}} : !cir.ptr<!s32i>, @_ZTIi
// CIR-NEXT:          cir.unreachable
// CIR:           } cleanup all {
// CIR-NEXT:        cir.call @_ZN1SD1Ev(%[[S]])
// CIR:           omp.terminator

// CIR-FLAT-LABEL: cir.func {{.*}} @_Z35throw_in_cleanup_no_try_in_parallelb(
// CIR-FLAT:         omp.parallel {
// CIR-FLAT:           %[[S:.*]] = cir.alloca "s"
// CIR-FLAT:           cir.try_throw %{{.*}} : !cir.ptr<!s32i>, @_ZTIi ^[[UNREACH:bb[0-9]+]], ^[[LPAD:bb[0-9]+]]
// CIR-FLAT-NEXT:    ^[[UNREACH]]:
// CIR-FLAT-NEXT:      cir.unreachable
// CIR-FLAT:         ^[[LPAD]]:
// CIR-FLAT-NEXT:      %[[TOK:.*]] = cir.eh.initiate cleanup : !cir.eh_token
// CIR-FLAT-NEXT:      cir.br ^[[CLEANUP:bb[0-9]+]](%[[TOK]] : !cir.eh_token)
// CIR-FLAT:         ^[[CLEANUP]](%[[C_TOK:.*]]: !cir.eh_token):
// CIR-FLAT-NEXT:      %[[CT:.*]] = cir.begin_cleanup %[[C_TOK]]
// CIR-FLAT-NEXT:      cir.call @_ZN1SD1Ev(%[[S]])
// CIR-FLAT-NEXT:      cir.end_cleanup %[[CT]]
// CIR-FLAT-NEXT:      cir.resume %[[C_TOK]] : !cir.eh_token
// CIR-FLAT:           omp.terminator
// CIR-FLAT-NEXT:    }
// CIR-FLAT-NEXT:    cir.return

// FIXME: Classic CodeGen calls __clang_call_terminate instead of letting the
// exception leave the outlined function.
// LLVM-LABEL: define internal void @_Z35throw_in_cleanup_no_try_in_parallelb..omp_par(
// LLVM-SAME:    personality ptr @__gxx_personality_v0
// LLVM:         %[[S:.*]] = alloca %struct.S
// LLVM:         invoke void @__cxa_throw(ptr %{{.*}}, ptr @_ZTIi, ptr null)
// LLVM-NEXT:            to label %[[UNREACH:.*]] unwind label %[[LPAD:.*]]
// LLVM:       [[LPAD]]:
// LLVM-NEXT:    %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:            cleanup
// LLVM:         call void @_ZN1SD1Ev(ptr {{.*}} %[[S]])
// LLVM:         resume { ptr, i32 }
// LLVM:       [[UNREACH]]:
// LLVM-NEXT:    unreachable

// OGCG-LABEL: define internal void @_Z35throw_in_cleanup_no_try_in_parallelb.omp_outlined(
// OGCG-SAME:    personality ptr @__gxx_personality_v0
// OGCG:         invoke void @__cxa_throw(ptr %{{.*}}, ptr @_ZTIi, ptr null)
// OGCG-NEXT:            to label %[[UNREACH:.*]] unwind label %[[TERMINATE:.*]]
// OGCG:       [[TERMINATE]]:
// OGCG-NEXT:    %[[T_LP:.*]] = landingpad { ptr, i32 }
// OGCG-NEXT:            catch ptr null
// OGCG-NEXT:    %[[T_EXN:.*]] = extractvalue { ptr, i32 } %[[T_LP]], 0
// OGCG-NEXT:    call void @__clang_call_terminate(ptr %[[T_EXN]])
// OGCG:       [[UNREACH]]:
// OGCG-NEXT:    unreachable
