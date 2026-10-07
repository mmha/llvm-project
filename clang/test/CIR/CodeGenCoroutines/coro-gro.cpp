// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -DVOID_CORO %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH --implicit-check-not=@wrong
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir -DVOID_CORO %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -DVOID_CORO %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM --implicit-check-not=@wrong
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir-eh.ll
// RUN: FileCheck --input-file=%t-cir-eh.ll %s -check-prefix=LLVM-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// When get_return_object() does not return the return type of the coroutine,
// its result is kept in the __coro_gro variable, which is converted to the
// return value when the coroutine first returns to its caller, and then
// destroyed. That is the gro region of the cir.coroutine. The flattening runs
// it only in the ramp function, either after the first suspension or, if the
// coroutine completes without suspending, before the coroutine state is
// destroyed. The variable and the return value stay out of the coroutine
// frame, as in classic CodeGen.

#include "Inputs/coroutine.h"

struct Gro {
  int token;
  Gro(int);
  Gro(Gro &&) noexcept;
  ~Gro();
};

struct Job {
  int token;
  Job(Gro &&);
  struct promise_type {
    Gro get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

Job job(int n) { co_await std::suspend_always(); }

// The destructor of __coro_gro is the cleanup of the conversion.

// CIR-LABEL: cir.func coroutine {{.*}} @_Z3jobi(
// CIR:         %[[N:.*]] = cir.alloca "n" align(4) init coro_outside_frame : !cir.ptr<!s32i>
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval" align(4) coro_outside_frame : !cir.ptr<!rec_Job>
// CIR:         %[[N_COPY:.*]] = cir.alloca "n" align(4) init : !cir.ptr<!s32i>
// CIR:         %[[PROMISE:.*]] = cir.alloca "__promise" {{.*}} : !cir.ptr<!rec_Job3A3Apromise_type>
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(4) init coro_outside_frame : !cir.ptr<!rec_Gro>
// CIR:         cir.coroutine initialSuspend : {
// CIR-NEXT:      cir.call @_ZN3Job12promise_type17get_return_objectEv(%[[GRO]], %[[PROMISE]])
// CIR-NEXT:      cir.call @_ZN3Job12promise_type15initial_suspendEv(%[[PROMISE]])
// CIR:         }, finalSuspend : {
// CIR:         }, gro : {
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.call @_ZN3JobC1EO3Gro(%[[RETVAL]], %[[GRO]])
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:        cir.call @_ZN3GroD1Ev(%[[GRO]]){{.*}}
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }, destroy : {
// CIR:         }, exit : {
// CIR:           %[[RETVAL_INT:.*]] = cir.cast bitcast %[[RETVAL]] : !cir.ptr<!rec_Job> -> !cir.ptr<!s32i>
// CIR-NEXT:      %[[RET:.*]] = cir.load align(4) %[[RETVAL_INT]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:      cir.return %[[RET]] : !s32i

// LLVM-LABEL: define dso_local i32 @_Z3jobi(
// LLVM:         %[[N:.*]] = alloca i32, align 4, !coro.outside.frame ![[EMPTY:[0-9]+]]
// LLVM:         %[[RETVAL:.*]] = alloca %struct.Job, align 4, !coro.outside.frame ![[EMPTY]]
// LLVM:         %[[GRO:.*]] = alloca %struct.Gro, align 4, !coro.outside.frame ![[EMPTY]]
// LLVM:         call void @_ZN3Job12promise_type17get_return_objectEv(ptr {{.*}} %[[GRO]], ptr {{.*}})
// LLVM-NEXT:    call void @_ZN3Job12promise_type15initial_suspendEv(
// LLVM:         switch i8 %{{.*}}, label %[[INIT_SUSPENDED:[0-9]+]] [
// LLVM:       [[INIT_SUSPENDED]]:
// LLVM-NEXT:    br label %[[PRE_CONV:[0-9]+]]
// LLVM:         switch i8 %{{.*}}, label %[[AWAIT_SUSPENDED:[0-9]+]] [
// LLVM:       [[AWAIT_SUSPENDED]]:
// LLVM-NEXT:    br label %[[PRE_CONV]]
// LLVM:         switch i8 %{{.*}}, label %[[FINAL_SUSPENDED:[0-9]+]] [
// LLVM-NEXT:      i8 0, label %[[FINAL_READY:[0-9]+]]
// LLVM-NEXT:      i8 1, label %[[FINAL_DESTROYED:[0-9]+]]
// LLVM:       [[FINAL_SUSPENDED]]:
// LLVM-NEXT:    br label %[[PRE_CONV]]
// LLVM:       [[FINAL_READY]]:
// LLVM-NEXT:    call void @_ZNSt14suspend_always12await_resumeEv(
// LLVM-NEXT:    br label %[[FINAL_DONE:[0-9]+]]
// LLVM:       [[FINAL_DONE]]:
// LLVM-NEXT:    br label %[[PRE_CONV]]
// LLVM:       [[PRE_CONV]]:
// LLVM-NEXT:    %[[COMPLETED:.*]] = phi i1 [ false, %[[INIT_SUSPENDED]] ], [ false, %[[AWAIT_SUSPENDED]] ], [ false, %[[FINAL_SUSPENDED]] ], [ true, %[[FINAL_DONE]] ]
// LLVM-NEXT:    %[[IN_RAMP:.*]] = call i1 @llvm.coro.is_in_ramp()
// LLVM-NEXT:    br i1 %[[IN_RAMP]], label %{{.*}}, label %[[AFTER_CONV:[0-9]+]]
// LLVM:         call void @_ZN3JobC1EO3Gro(ptr {{.*}} %[[RETVAL]], ptr {{.*}} %[[GRO]])
// LLVM-NEXT:    br label %[[DTOR:[0-9]+]]
// LLVM:       [[DTOR]]:
// LLVM-NEXT:    call void @_ZN3GroD1Ev(ptr {{.*}} %[[GRO]])
// LLVM:       [[AFTER_CONV]]:
// LLVM-NEXT:    br i1 %[[COMPLETED]], label %[[DESTROY:[0-9]+]], label %[[RET:[0-9]+]]
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(
// LLVM:       [[RET]]:
// LLVM-NEXT:    call void @llvm.coro.end(ptr null, i1 false, token none)
// LLVM-NEXT:    %[[R:.*]] = load i32, ptr %[[RETVAL]]
// LLVM-NEXT:    ret i32 %[[R]]

// A conversion that throws destroys __coro_gro on the way out.

// LLVM-EH-LABEL: define dso_local i32 @_Z3jobi(
// LLVM-EH:         %[[GRO:.*]] = alloca %struct.Gro, align 4, !coro.outside.frame
// LLVM-EH:         call i1 @llvm.coro.is_in_ramp()
// LLVM-EH:         invoke void @_ZN3JobC1EO3Gro(ptr {{.*}}, ptr {{.*}} %[[GRO]])
// LLVM-EH-NEXT:      to label %{{.*}} unwind label %[[LPAD:[0-9]+]]
// LLVM-EH:       [[LPAD]]:
// LLVM-EH-NEXT:    landingpad { ptr, i32 }
// LLVM-EH-NEXT:      cleanup
// LLVM-EH-NOT:     load
// LLVM-EH:         call void @_ZN3GroD1Ev(ptr {{.*}} %[[GRO]])
// LLVM-EH:         resume { ptr, i32 }

// Classic CodeGen guards the destructor with a flag that is false until
// get_return_object() has returned.

// OGCG-LABEL: define dso_local i32 @_Z3jobi(
// OGCG:         %retval = alloca %struct.Job, align 4, !coro.outside.frame ![[EMPTY:[0-9]+]]
// OGCG:         %n.addr = alloca i32, align 4, !coro.outside.frame ![[EMPTY]]
// OGCG:         %gro.active = alloca i1, align 1
// OGCG:         %__coro_gro = alloca %struct.Gro, align 4, !coro.outside.frame ![[EMPTY]]
// OGCG:         store i1 false, ptr %gro.active
// OGCG:         call void @_ZN3Job12promise_type17get_return_objectEv(ptr {{.*}} %__coro_gro, ptr {{.*}} %__promise)
// OGCG-NEXT:    store i1 true, ptr %gro.active
// OGCG:       pre.gvo.conv:
// OGCG-NEXT:    %[[COMPLETED:.*]] = phi i1 [ true, %{{.*}} ], [ false, %final.suspend ], [ false, %await.suspend ], [ false, %init.suspend ]
// OGCG-NEXT:    %InRamp = call i1 @llvm.coro.is_in_ramp()
// OGCG-NEXT:    br i1 %InRamp, label %gro.conv, label %after.gro.conv
// OGCG:       gro.conv:
// OGCG-NEXT:    call void @_ZN3JobC1EO3Gro(ptr {{.*}} %retval, ptr {{.*}} %__coro_gro)
// OGCG-NEXT:    %cleanup.is_active = load i1, ptr %gro.active
// OGCG:         call void @_ZN3GroD1Ev(ptr {{.*}} %__coro_gro)
// OGCG:       after.gro.conv:
// OGCG-NEXT:    br i1 %[[COMPLETED]], label %{{.*}}, label %coro.ret

// A get_return_object() result without a destructor, and a return value of
// pointer type. This coroutine never suspends: it runs to completion in the
// ramp function, which converts the result before the coroutine state is
// destroyed.

struct Handle {
  void *address;
};

struct HandleTask {
  void *address;
  HandleTask(Handle) noexcept;
  struct promise_type {
    Handle get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

HandleTask handle_task() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z11handle_taskv(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval" align(8) coro_outside_frame : !cir.ptr<!rec_HandleTask>
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(8) init coro_outside_frame : !cir.ptr<!rec_Handle>
// CIR:         cir.coroutine initialSuspend : {
// CIR:           cir.call @_ZN10HandleTask12promise_type17get_return_objectEv(
// CIR:         }, gro : {
// CIR-NEXT:      cir.copy %[[GRO]] align(8) to %[[ARG:.*]] align(8) : !cir.ptr<!rec_Handle>
// CIR-NOT:       cir.cleanup.scope
// CIR:           cir.call @_ZN10HandleTaskC1E6Handle(%[[RETVAL]], %{{.*}})
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }, destroy : {

// LLVM-LABEL: define dso_local ptr @_Z11handle_taskv(
// LLVM:         %[[RETVAL:.*]] = alloca %struct.HandleTask, align 8, !coro.outside.frame ![[EMPTY]]
// LLVM:         %[[GRO:.*]] = alloca %struct.Handle, align 8, !coro.outside.frame ![[EMPTY]]
// LLVM:         call i1 @llvm.coro.is_in_ramp()
// LLVM:         call void @_ZN10HandleTaskC1E6Handle(ptr {{.*}} %[[RETVAL]], ptr %{{.*}})
// LLVM:         br i1 %{{.*}}, label %[[DESTROY:[0-9]+]], label %{{[0-9]+}}
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call ptr @llvm.coro.free(

// OGCG-LABEL: define dso_local ptr @_Z11handle_taskv(
// OGCG:         %retval = alloca %struct.HandleTask, align 8, !coro.outside.frame ![[EMPTY]]
// OGCG:         %__coro_gro = alloca %struct.Handle, align 8, !coro.outside.frame ![[EMPTY]]
// OGCG:       gro.conv:
// OGCG:         call void @_ZN10HandleTaskC1E6Handle(ptr {{.*}} %retval, ptr %{{.*}})

// The temporaries of the get_return_object() call end with its
// full-expression, before initial_suspend, as in GCC. Classic CodeGen destroys
// them only after the conversion, in the ramp function.

struct Tmp {
  Tmp();
  ~Tmp();
};

struct TmpJob {
  int token;
  TmpJob(Gro &&);
  struct promise_type {
    Gro get_return_object(Tmp = Tmp()) noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

TmpJob tmp_job() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z7tmp_jobv(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval"
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro"
// CIR:         %[[TMP:.*]] = cir.alloca "agg.tmp0"
// CIR:         cir.coroutine initialSuspend : {
// CIR-NEXT:      cir.call @_ZN3TmpC1Ev(%[[TMP]])
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.call @_ZN6TmpJob12promise_type17get_return_objectE3Tmp(%[[GRO]], %{{.*}}, %[[TMP]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      } cleanup {{.*}} {
// CIR-NEXT:        cir.call @_ZN3TmpD1Ev(%[[TMP]])
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.call @_ZN6TmpJob12promise_type15initial_suspendEv(
// CIR:         }, gro : {
// CIR-NOT:       @_ZN3TmpD1Ev
// CIR:           cir.call @_ZN6TmpJobC1EO3Gro(%[[RETVAL]], %[[GRO]])
// CIR-NOT:       @_ZN3TmpD1Ev
// CIR:         }, destroy : {

// OGCG-LABEL: define dso_local i32 @_Z7tmp_jobv(
// OGCG:         call void @_ZN3TmpC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG-NEXT:    call void @_ZN6TmpJob12promise_type17get_return_objectE3Tmp(ptr {{.*}} %__coro_gro, ptr {{.*}} %__promise, ptr {{.*}} %[[TMP]])
// OGCG-NOT:     @_ZN3TmpD1Ev
// OGCG:       gro.conv:
// OGCG-NEXT:    call void @_ZN6TmpJobC1EO3Gro(ptr {{.*}} %retval, ptr {{.*}} %__coro_gro)
// OGCG-NEXT:    call void @_ZN3TmpD1Ev(ptr {{.*}} %[[TMP]])

// A return value that is returned through a pointer to the return slot is
// initialized in place by the conversion.

struct BigJob {
  int tokens[8];
  BigJob(Gro &&);
  struct promise_type {
    Gro get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

BigJob big_job() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z7big_jobv(
// CIR-SAME:      %[[RET:[^:]*]]: !cir.ptr<!rec_BigJob>
// CIR-NOT:       "__retval"
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(4) init coro_outside_frame : !cir.ptr<!rec_Gro>
// CIR:         }, gro : {
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.call @_ZN6BigJobC1EO3Gro(%[[RET]], %[[GRO]])

// LLVM-LABEL: define dso_local void @_Z7big_jobv(
// LLVM-SAME:      ptr {{.*}} sret(%struct.BigJob) {{.*}} %[[RET:[0-9]+]])
// LLVM:         %[[GRO:.*]] = alloca %struct.Gro, align 4, !coro.outside.frame
// LLVM:         call i1 @llvm.coro.is_in_ramp()
// LLVM:         call void @_ZN6BigJobC1EO3Gro(ptr {{.*}} %[[RET]], ptr {{.*}} %[[GRO]])
// LLVM:         call void @_ZN3GroD1Ev(ptr {{.*}} %[[GRO]])
// LLVM:         ret void

// OGCG-LABEL: define dso_local void @_Z7big_jobv(
// OGCG:       gro.conv:
// OGCG-NEXT:    call void @_ZN6BigJobC1EO3Gro(ptr {{.*}} %agg.result, ptr {{.*}} %__coro_gro)

// A coroutine that returns a reference binds it to the result of the
// conversion, like a return statement. Classic CodeGen copies the referenced
// object into the return slot instead, so these have no OGCG checks.

struct Obj {
  int v;
};

struct Wrapper {
  Obj *p;
  ~Wrapper();
  operator Obj &() const;
};

template <typename T> struct RefPromise {
  T get_return_object() noexcept;
  std::suspend_never initial_suspend() noexcept;
  std::suspend_never final_suspend() noexcept;
  void return_void() noexcept;
  void unhandled_exception() noexcept;
};

struct IntRefTag {};
struct ObjRefTag {};
struct ConvRefTag {};

template <> struct std::coroutine_traits<int &, IntRefTag> {
  using promise_type = RefPromise<int &>;
};
template <> struct std::coroutine_traits<Obj &, ObjRefTag> {
  using promise_type = RefPromise<Obj &>;
};
template <> struct std::coroutine_traits<Obj &, ConvRefTag> {
  using promise_type = RefPromise<Wrapper>;
};

int &int_ref(IntRefTag) { co_return; }
Obj &obj_ref(ObjRefTag) { co_return; }
Obj &conv_ref(ConvRefTag) { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z7int_ref9IntRefTag(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval" align(8) coro_outside_frame : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(8) init const coro_outside_frame : !cir.ptr<!cir.ptr<!s32i>>
// CIR:         }, gro : {
// CIR-NEXT:      %[[REF:.*]] = cir.load %[[GRO]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR-NEXT:      cir.store align(8) %[[REF]], %[[RETVAL]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }, destroy : {

// CIR-LABEL: cir.func coroutine {{.*}} @_Z7obj_ref9ObjRefTag(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval" align(8) coro_outside_frame : !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(8) init const coro_outside_frame : !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR:         }, gro : {
// CIR-NEXT:      %[[REF:.*]] = cir.load %[[GRO]] : !cir.ptr<!cir.ptr<!rec_Obj>>, !cir.ptr<!rec_Obj>
// CIR-NEXT:      cir.store align(8) %[[REF]], %[[RETVAL]] : !cir.ptr<!rec_Obj>, !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR-NEXT:      cir.yield
// CIR-NEXT:    }, destroy : {

// CIR-LABEL: cir.func coroutine {{.*}} @_Z8conv_ref10ConvRefTag(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval" align(8) coro_outside_frame : !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(8) init coro_outside_frame : !cir.ptr<!rec_Wrapper>
// CIR:         }, gro : {
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        %[[REF:.*]] = cir.call @_ZNK7WrappercvR3ObjEv(%[[GRO]])
// CIR-NEXT:        cir.store align(8) %[[REF]], %[[RETVAL]] : !cir.ptr<!rec_Obj>, !cir.ptr<!cir.ptr<!rec_Obj>>
// CIR-NEXT:        cir.yield
// CIR-NEXT:      } cleanup {{.*}} {
// CIR-NEXT:        cir.call @_ZN7WrapperD1Ev(%[[GRO]])

// LLVM-LABEL: define dso_local {{.*}} ptr @_Z7int_ref9IntRefTag(
// LLVM:         %[[RETVAL:.*]] = alloca ptr, align 8, !coro.outside.frame
// LLVM:         %[[GRO:.*]] = alloca ptr, align 8, !coro.outside.frame
// LLVM:         %[[REF:.*]] = call {{.*}} ptr @_ZN10RefPromiseIRiE17get_return_objectEv(
// LLVM-NEXT:    store ptr %[[REF]], ptr %[[GRO]], align 8
// LLVM:         call i1 @llvm.coro.is_in_ramp()
// LLVM:         %[[CONV:.*]] = load ptr, ptr %[[GRO]], align 8
// LLVM-NEXT:    store ptr %[[CONV]], ptr %[[RETVAL]], align 8
// LLVM:         %[[R:.*]] = load ptr, ptr %[[RETVAL]], align 8
// LLVM-NEXT:    ret ptr %[[R]]

// When the variable has the return type of the coroutine up to cv-qualifiers,
// returning it is an NRVO return (GH148953): the variable is constructed in the
// return slot, and nothing converts or destroys it. Classic CodeGen copies the
// return value onto itself and destroys it instead.

struct ConstTask {
  struct promise_type {
    ConstTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
  ConstTask() noexcept;
  ConstTask(const ConstTask &) noexcept;
  ~ConstTask();
};

const ConstTask const_task() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z10const_taskv(
// CIR-SAME:      %[[RET:[^:]*]]: !cir.ptr<!rec_ConstTask>
// CIR-NOT:       "__coro_gro"
// CIR-NOT:       "nrvo"
// CIR:         cir.coroutine initialSuspend : {
// CIR-NEXT:      cir.call @_ZN9ConstTask12promise_type17get_return_objectEv(%[[RET]], %{{.*}})
// CIR-NOT:       @_ZN9ConstTaskC1ERKS_
// CIR-NOT:       @_ZN9ConstTaskD1Ev
// CIR-NOT:       gro : {
// CIR:         }, exit : {

// LLVM-LABEL: define dso_local void @_Z10const_taskv(
// LLVM-SAME:      ptr {{.*}} sret(%struct.ConstTask) {{.*}} %[[RET:[0-9]+]])
// LLVM:         call void @_ZN9ConstTask12promise_type17get_return_objectEv(ptr {{.*}} %[[RET]], ptr {{.*}})
// LLVM-NOT:     @_ZN9ConstTaskC1ERKS_
// LLVM-NOT:     @_ZN9ConstTaskD1Ev
// LLVM-NOT:     @llvm.coro.is_in_ramp
// LLVM:         ret void

// OGCG-LABEL: define dso_local void @_Z10const_taskv(
// OGCG:         call void @_ZN9ConstTask12promise_type17get_return_objectEv(ptr {{.*}} %agg.result, ptr {{.*}} %__promise)
// OGCG:       gro.conv:
// OGCG-NEXT:    call void @_ZN9ConstTaskC1ERKS_(ptr {{.*}} %agg.result, ptr {{.*}} %agg.result)

// A return value that is not returned through a pointer to the return slot is
// read after the coroutine may have been destroyed, so it stays out of the
// coroutine frame.

struct TrivialTask {
  int v;
  struct promise_type {
    TrivialTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

const TrivialTask const_trivial_task() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z18const_trivial_taskv(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval" align(4) coro_outside_frame : !cir.ptr<!rec_TrivialTask>
// CIR:         cir.coroutine initialSuspend : {
// CIR-NEXT:      %[[R:.*]] = cir.call @_ZN11TrivialTask12promise_type17get_return_objectEv(
// CIR:           cir.store align(4) %{{.*}}, %[[RETVAL]]
// CIR-NOT:       gro : {
// CIR:         }, exit : {

// OGCG-LABEL: define dso_local i32 @_Z18const_trivial_taskv(
// OGCG:         %retval = alloca %struct.TrivialTask, align 4, !coro.outside.frame

// The variable can be a reference (CWG2563): get_return_object() returns the
// promise by lvalue or rvalue reference, and the return value is constructed
// from it without copying the promise. The promise and handle constructors
// that must not be called call wrong(), which the CIR and LLVM runs reject.

extern "C" void wrong();

template <bool LValue> struct RefGroPromise {
  RefGroPromise() = default;
  RefGroPromise(const RefGroPromise &) { wrong(); }
  RefGroPromise(RefGroPromise &&) { wrong(); }
  auto &&get_return_object() {
    if constexpr (LValue)
      return *this;
    else
      return static_cast<RefGroPromise &&>(*this);
  }
  std::suspend_never initial_suspend() const noexcept;
  std::suspend_never final_suspend() const noexcept;
  void return_void() const noexcept;
  void unhandled_exception() const noexcept;
};

template <bool LValue> struct RefGroHandle {
  using promise_type = RefGroPromise<LValue>;
  RefGroHandle(promise_type &) {
    if constexpr (!LValue)
      wrong();
  }
  RefGroHandle(promise_type &&) {
    if constexpr (LValue)
      wrong();
  }
};

RefGroHandle<true> lvalue_gro() { co_return; }
RefGroHandle<false> rvalue_gro() { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z10lvalue_grov(
// CIR:         %[[RETVAL:.*]] = cir.alloca "__retval"
// CIR:         %[[GRO:.*]] = cir.alloca "__coro_gro" align(8) init const coro_outside_frame : !cir.ptr<!cir.ptr<!rec_RefGroPromise3Ctrue3E>>
// CIR:         }, gro : {
// CIR-NEXT:      %[[P:.*]] = cir.load %[[GRO]]
// CIR-NEXT:      cir.call @_ZN12RefGroHandleILb1EEC1ER13RefGroPromiseILb1EE(%[[RETVAL]], %[[P]])

// CIR-LABEL: cir.func coroutine {{.*}} @_Z10rvalue_grov(
// CIR:         }, gro : {
// CIR:           cir.call @_ZN12RefGroHandleILb0EEC1EO13RefGroPromiseILb0EE(

#ifdef VOID_CORO
// A coroutine that returns void discards the result of get_return_object(), as
// a full-expression of its own. Classic CodeGen asserts on it, so it has no
// OGCG checks.

struct VoidTag {};
template <> struct std::coroutine_traits<void, VoidTag> {
  struct promise_type {
    Gro get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

void void_coro(VoidTag) { co_return; }

// CIR-LABEL: cir.func coroutine {{.*}} @_Z9void_coro7VoidTag(
// CIR:         %[[TMP:.*]] = cir.alloca "agg.tmp.ensured" align(4) : !cir.ptr<!rec_Gro>
// CIR:         cir.coroutine initialSuspend : {
// CIR-NEXT:      cir.call @_ZNSt16coroutine_traitsIvJ7VoidTagEE12promise_type17get_return_objectEv(%[[TMP]], %{{.*}})
// CIR-NEXT:      cir.cleanup.scope {
// CIR-NEXT:        cir.yield
// CIR-NEXT:      } cleanup {{.*}} {
// CIR-NEXT:        cir.call @_ZN3GroD1Ev(%[[TMP]]){{.*}}
// CIR-NEXT:        cir.yield
// CIR-NEXT:      }
// CIR-NEXT:      cir.call @_ZNSt16coroutine_traitsIvJ7VoidTagEE12promise_type15initial_suspendEv(
// CIR:         }, finalSuspend : {
// CIR-NOT:     gro : {
// CIR:         }, destroy : {

// LLVM-LABEL: define dso_local void @_Z9void_coro7VoidTag(
// LLVM:         call void @_ZNSt16coroutine_traitsIvJ7VoidTagEE12promise_type17get_return_objectEv(ptr {{.*}} %[[TMP:[0-9]+]], ptr {{.*}})
// LLVM:         call void @_ZN3GroD1Ev(ptr {{.*}} %[[TMP]])
// LLVM:         call void @_ZNSt16coroutine_traitsIvJ7VoidTagEE12promise_type15initial_suspendEv(
// LLVM-NOT:     @llvm.coro.is_in_ramp
// LLVM:         ret void
#endif

// LLVM: ![[EMPTY]] = !{}
// OGCG: ![[EMPTY]] = !{}
