// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fopenacc %s -o %t-acc.cir
// RUN: FileCheck --input-file=%t-acc.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fno-clangir-call-conv-lowering %s -o %t-gen.cir
// RUN: FileCheck --input-file=%t-gen.cir %s -check-prefix=CIRGEN
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// The wrappers are functions without a declaration. The -fopenacc run checks
// that creating them does not look for OpenACC routine directives.

#include "Inputs/coroutine.h"

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
    std::suspend_always yield_value(int) noexcept;
  };
};

// Symmetric transfer: await_suspend returns the coroutine to resume next.
struct TransferAwaiter {
  bool await_ready() noexcept;
  std::coroutine_handle<> await_suspend(std::coroutine_handle<Task::promise_type>) noexcept;
  void await_resume() noexcept;
};

struct TypedTransferAwaiter {
  bool await_ready() noexcept;
  std::coroutine_handle<Task::promise_type> await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

// CIR: cir.func coroutine {{.*}} @_Z8transferv()
// CIR:   %[[FRAME:.*]] = cir.coro.intrinsic.begin

// The wrapper is called through the handle form of the intrinsic. The
// coroutine then suspends like for a void await_suspend; the coroutine split
// resumes the returned coroutine with a tail call.

// CIR:   cir.await(user, ready : {
// CIR-NEXT:   cir.call @_ZN15TransferAwaiter11await_readyEv(%[[TRANSFER_ADDR:[^)]*]])
// CIR:   }, suspend : {
// CIR-NEXT:   %[[TRANSFER:.*]] = cir.cast bitcast %[[TRANSFER_ADDR]] : !cir.ptr<!rec_TransferAwaiter> -> !cir.ptr<!void>
// CIR-NEXT:   cir.coro.intrinsic.await.suspend.handle(%[[TRANSFER]], %[[FRAME]], @_Z8transferv.__await_suspend_wrapper__await) : (!cir.ptr<!void>, !cir.ptr<!void>) -> ()
// CIR-NEXT:   cir.coro.suspend_point
// CIR-NEXT: }, resume : {

// CIR:   cir.await(user, ready : {
// CIR-NEXT:   cir.call @_ZN20TypedTransferAwaiter11await_readyEv(%[[TYPED_TRANSFER_ADDR:[^)]*]])
// CIR:   }, suspend : {
// CIR-NEXT:   %[[TYPED_TRANSFER:.*]] = cir.cast bitcast %[[TYPED_TRANSFER_ADDR]] : !cir.ptr<!rec_TypedTransferAwaiter> -> !cir.ptr<!void>
// CIR-NEXT:   cir.coro.intrinsic.await.suspend.handle(%[[TYPED_TRANSFER]], %[[FRAME]], @_Z8transferv.__await_suspend_wrapper__await2) : (!cir.ptr<!void>, !cir.ptr<!void>) -> ()
// CIR-NEXT:   cir.coro.suspend_point
// CIR-NEXT: }, resume : {

// LLVM: define dso_local void @_Z8transferv()
// LLVM:   %[[FRAME:.*]] = call ptr @llvm.coro.begin(
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z8transferv.__await_suspend_wrapper__init)
// LLVM:   %[[SAVE:.*]] = call token @llvm.coro.save(ptr null)
// LLVM-NEXT:   call void @llvm.coro.await.suspend.handle(ptr %{{.*}}, ptr %[[FRAME]], ptr @_Z8transferv.__await_suspend_wrapper__await)
// LLVM-NEXT:   call i8 @llvm.coro.suspend(token %[[SAVE]], i1 false)
// LLVM:   %[[SAVE2:.*]] = call token @llvm.coro.save(ptr null)
// LLVM-NEXT:   call void @llvm.coro.await.suspend.handle(ptr %{{.*}}, ptr %[[FRAME]], ptr @_Z8transferv.__await_suspend_wrapper__await2)
// LLVM-NEXT:   call i8 @llvm.coro.suspend(token %[[SAVE2]], i1 false)

// OGCG: define dso_local void @_Z8transferv()
// OGCG:   %[[FRAME:.*]] = call ptr @llvm.coro.begin(
// OGCG: await.suspend:
// OGCG:   %[[SAVE:.*]] = call token @llvm.coro.save(ptr null)
// OGCG:   call void @llvm.coro.await.suspend.handle(ptr %[[TRANSFER_ADDR:.*]], ptr %[[FRAME]], ptr @_Z8transferv.__await_suspend_wrapper__await)
// OGCG:   call i8 @llvm.coro.suspend(token %[[SAVE]], i1 false)
// OGCG: await2.suspend:
// OGCG:   %[[SAVE2:.*]] = call token @llvm.coro.save(ptr null)
// OGCG:   call void @llvm.coro.await.suspend.handle(ptr %[[TYPED_TRANSFER_ADDR:.*]], ptr %[[FRAME]], ptr @_Z8transferv.__await_suspend_wrapper__await2)
// OGCG:   call i8 @llvm.coro.suspend(token %[[SAVE2]], i1 false)
Task transfer() {
  co_await TransferAwaiter{};
  co_await TypedTransferAwaiter{};
}

// The wrappers return the address() of the handle that await_suspend returns.
// The coroutine_handle of Inputs/coroutine.h is an empty class, whose values
// the calling convention lowering replaces with #cir.poison, so the CIRGEN run
// checks the flow from the await_suspend call to address() before that
// lowering.

// CIRGEN-LABEL: cir.func always_inline internal private @_Z8transferv.__await_suspend_wrapper__await(
// CIRGEN:   %[[AWAITER_ARG_ADDR:.*]] = cir.alloca "" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIRGEN:   %[[FRAME_ARG_ADDR:.*]] = cir.alloca "" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIRGEN:   %[[HANDLE_ADDR:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_std3A3Acoroutine_handle3Cvoid3E>
// CIRGEN:   %[[ARG_TMP:.*]] = cir.alloca "agg.tmp0" {{.*}} : !cir.ptr<!rec_std3A3Acoroutine_handle3CTask3A3Apromise_type3E>
// CIRGEN:   %[[AWAITER_VOID_PTR:.*]] = cir.load{{.*}} %[[AWAITER_ARG_ADDR]]
// CIRGEN:   %[[AWAITER_PTR:.*]] = cir.cast bitcast %[[AWAITER_VOID_PTR]] : !cir.ptr<!void> -> !cir.ptr<!rec_TransferAwaiter>
// CIRGEN:   %[[FRAME_PTR:.*]] = cir.load{{.*}} %[[FRAME_ARG_ADDR]]
// CIRGEN:   %[[FROM:.*]] = cir.call @_ZNSt16coroutine_handleIN4Task12promise_typeEE12from_addressEPv(%[[FRAME_PTR]])
// CIRGEN:   cir.store{{.*}} %[[FROM]], %[[ARG_TMP]] :
// CIRGEN:   %[[ARG:.*]] = cir.load{{.*}} %[[ARG_TMP]]
// CIRGEN:   %[[RES:.*]] = cir.call @_ZN15TransferAwaiter13await_suspendESt16coroutine_handleIN4Task12promise_typeEE(%[[AWAITER_PTR]], %[[ARG]])
// CIRGEN-NEXT:   cir.store{{.*}} %[[RES]], %[[HANDLE_ADDR]]
// CIRGEN-NEXT:   %[[ADDRESS:.*]] = cir.call @_ZNKSt16coroutine_handleIvE7addressEv(%[[HANDLE_ADDR]])
// CIRGEN-NEXT:   cir.store{{.*}} %[[ADDRESS]], %[[RETVAL:.*]] :
// CIRGEN-NEXT:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIRGEN-NEXT:   cir.return %[[RET]] : !cir.ptr<!void>

// CIR: cir.func always_inline internal private @_Z8transferv.__await_suspend_wrapper__await(%[[AWAITER_ARG:[^:]*]]: !cir.ptr<!void> {llvm.nonnull, llvm.noundef} {{.*}}, %[[FRAME_ARG:[^:]*]]: !cir.ptr<!void> {llvm.noundef} {{.*}}) -> (!cir.ptr<!void> {llvm.noundef})
// CIR:   %[[AWAITER_ARG_ADDR:.*]] = cir.alloca "" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[FRAME_ARG_ADDR:.*]] = cir.alloca "" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[RETVAL:.*]] = cir.alloca "__retval" {{.*}} : !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[HANDLE_ADDR:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_std3A3Acoroutine_handle3Cvoid3E>
// CIR:   cir.store %[[AWAITER_ARG]], %[[AWAITER_ARG_ADDR]]
// CIR:   cir.store %[[FRAME_ARG]], %[[FRAME_ARG_ADDR]]
// CIR:   %[[AWAITER_VOID_PTR:.*]] = cir.load{{.*}} %[[AWAITER_ARG_ADDR]]
// CIR:   %[[AWAITER_PTR:.*]] = cir.cast bitcast %[[AWAITER_VOID_PTR]] : !cir.ptr<!void> -> !cir.ptr<!rec_TransferAwaiter>
// CIR:   %[[FRAME_PTR:.*]] = cir.load{{.*}} %[[FRAME_ARG_ADDR]]
// CIR:   cir.call @_ZNSt16coroutine_handleIN4Task12promise_typeEE12from_addressEPv(%[[FRAME_PTR]])
// CIR:   cir.call @_ZN15TransferAwaiter13await_suspendESt16coroutine_handleIN4Task12promise_typeEE(%[[AWAITER_PTR]])
// CIR:   %[[HANDLE:.*]] = cir.const #cir.poison : !rec_std3A3Acoroutine_handle3Cvoid3E
// CIR:   cir.store{{.*}} %[[HANDLE]], %[[HANDLE_ADDR]]
// CIR:   %[[ADDRESS:.*]] = cir.call @_ZNKSt16coroutine_handleIvE7addressEv(%[[HANDLE_ADDR]])
// CIR:   cir.store{{.*}} %[[ADDRESS]], %[[RETVAL]]
// CIR:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR:   cir.return %[[RET]] : !cir.ptr<!void>

// LLVM: define internal noundef ptr @_Z8transferv.__await_suspend_wrapper__await(ptr noundef nonnull %{{.*}}, ptr noundef %{{.*}})
// LLVM:   call void @_ZN15TransferAwaiter13await_suspendESt16coroutine_handleIN4Task12promise_typeEE(
// LLVM:   %[[ADDRESS:.*]] = call noundef ptr @_ZNKSt16coroutine_handleIvE7addressEv(
// LLVM:   store ptr %[[ADDRESS]], ptr %[[RETVAL:.*]],
// LLVM:   %[[RET:.*]] = load ptr, ptr %[[RETVAL]]
// LLVM:   ret ptr %[[RET]]

// OGCG: define internal noundef ptr @_Z8transferv.__await_suspend_wrapper__await(ptr noundef nonnull %[[AWAITER_ARG:.*]], ptr noundef %[[FRAME_ARG:.*]])
// OGCG:   %[[AWAITER_ARG_ADDR:.*]] = alloca ptr
// OGCG:   %[[FRAME_ARG_ADDR:.*]] = alloca ptr
// OGCG:   %[[HANDLE_ADDR:.*]] = alloca %"struct.std::coroutine_handle"
// OGCG:   store ptr %[[AWAITER_ARG]], ptr %[[AWAITER_ARG_ADDR]]
// OGCG:   store ptr %[[FRAME_ARG]], ptr %[[FRAME_ARG_ADDR]]
// OGCG:   %[[AWAITER_PTR:.*]] = load ptr, ptr %[[AWAITER_ARG_ADDR]]
// OGCG:   %[[FRAME_PTR:.*]] = load ptr, ptr %[[FRAME_ARG_ADDR]]
// OGCG:   call void @_ZNSt16coroutine_handleIN4Task12promise_typeEE12from_addressEPv(ptr noundef %[[FRAME_PTR]])
// OGCG:   call void @_ZN15TransferAwaiter13await_suspendESt16coroutine_handleIN4Task12promise_typeEE(ptr noundef nonnull align 1 dereferenceable(1) %[[AWAITER_PTR]])
// OGCG:   %[[ADDRESS:.*]] = call noundef ptr @_ZNKSt16coroutine_handleIvE7addressEv(ptr noundef nonnull align 1 dereferenceable(1) %[[HANDLE_ADDR]])
// OGCG:   ret ptr %[[ADDRESS]]

// CIR: cir.func always_inline internal private @_Z8transferv.__await_suspend_wrapper__await2({{.*}}) -> (!cir.ptr<!void> {llvm.noundef})
// CIR:   cir.call @_ZN20TypedTransferAwaiter13await_suspendESt16coroutine_handleIvE(
// CIR:   %[[ADDRESS:.*]] = cir.call @_ZNKSt16coroutine_handleIN4Task12promise_typeEE7addressEv(
// CIR:   cir.store{{.*}} %[[ADDRESS]], %[[RETVAL:.*]] :
// CIR:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR:   cir.return %[[RET]] : !cir.ptr<!void>

// OGCG: define internal noundef ptr @_Z8transferv.__await_suspend_wrapper__await2(
// OGCG:   call void @_ZN20TypedTransferAwaiter13await_suspendESt16coroutine_handleIvE(
// OGCG:   %[[ADDRESS:.*]] = call noundef ptr @_ZNKSt16coroutine_handleIN4Task12promise_typeEE7addressEv(
// OGCG:   ret ptr %[[ADDRESS]]

struct VoidAwaiter {
  bool await_ready() noexcept;
  void await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

// Every suspend point gets its own wrapper, named like in classic codegen.

// CIR: cir.func coroutine {{.*}} @_Z5namesv()
// CIR:   cir.coro.intrinsic.await.suspend.void({{.*}}, @_Z5namesv.__await_suspend_wrapper__init)
// CIR:   cir.coro.intrinsic.await.suspend.void({{.*}}, @_Z5namesv.__await_suspend_wrapper__await)
// CIR:   cir.coro.intrinsic.await.suspend.void({{.*}}, @_Z5namesv.__await_suspend_wrapper__await2)
// CIR:   cir.coro.intrinsic.await.suspend.void({{.*}}, @_Z5namesv.__await_suspend_wrapper__yield)
// CIR:   cir.coro.intrinsic.await.suspend.void({{.*}}, @_Z5namesv.__await_suspend_wrapper__yield2)
// CIR:   cir.coro.intrinsic.await.suspend.handle({{.*}}, @_Z5namesv.__await_suspend_wrapper__await3)
// CIR:   cir.coro.intrinsic.await.suspend.void({{.*}}, @_Z5namesv.__await_suspend_wrapper__final)

// LLVM: define dso_local void @_Z5namesv()
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__init)
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__await)
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__await2)
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__yield)
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__yield2)
// LLVM:   call void @llvm.coro.await.suspend.handle({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__await3)
// LLVM:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__final)

// OGCG: define dso_local void @_Z5namesv()
// OGCG:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__init)
// OGCG:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__await)
// OGCG:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__await2)
// OGCG:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__yield)
// OGCG:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__yield2)
// OGCG:   call void @llvm.coro.await.suspend.handle({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__await3)
// OGCG:   call void @llvm.coro.await.suspend.void({{.*}}, ptr @_Z5namesv.__await_suspend_wrapper__final)
Task names() {
  co_await VoidAwaiter{};
  co_await VoidAwaiter{};
  co_yield 1;
  co_yield 2;
  co_await TransferAwaiter{};
}

// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__init(
// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__await(
// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__await2(
// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__yield(
// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__yield2(
// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__await3(
// CIR: cir.func always_inline internal private @_Z5namesv.__await_suspend_wrapper__final(

// A wrapper gets its own cleanup scopes, here for the temporary in a default
// argument of await_suspend.

struct Tmp {
  Tmp();
  ~Tmp();
};
int use(const Tmp &);

struct CleanupAwaiter {
  bool await_ready() noexcept;
  bool await_suspend(std::coroutine_handle<>, int = use(Tmp())) noexcept;
  void await_resume() noexcept;
};

Task cleanup() { co_await CleanupAwaiter{}; }

// CIR: cir.func always_inline internal private @_Z7cleanupv.__await_suspend_wrapper__await(
// CIR:   %[[TMP:.*]] = cir.alloca "ref.tmp0" {{.*}} : !cir.ptr<!rec_Tmp>
// CIR:   cir.call @_ZN3TmpC1Ev(%[[TMP]])
// CIR:   cir.cleanup.scope {
// CIR:     %[[ARG:.*]] = cir.call @_Z3useRK3Tmp(%[[TMP]])
// CIR:     cir.call @_ZN14CleanupAwaiter13await_suspendESt16coroutine_handleIvEi(%{{.*}}, %[[ARG]])
// CIR:     cir.yield
// CIR-NOEH: } cleanup normal {
// CIR-EH:   } cleanup all {
// CIR:     cir.call @_ZN3TmpD1Ev(%[[TMP]])
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.return

// OGCG: define internal noundef zeroext i1 @_Z7cleanupv.__await_suspend_wrapper__await(
// OGCG:   call void @_ZN3TmpC1Ev(ptr {{.*}} %[[TMP:.*]])
// OGCG:   %[[ARG:.*]] = call noundef i32 @_Z3useRK3Tmp(ptr {{.*}} %[[TMP]])
// OGCG:   call noundef zeroext i1 @_ZN14CleanupAwaiter13await_suspendESt16coroutine_handleIvEi(ptr {{.*}}, i32 noundef %[[ARG]])
// OGCG:   call void @_ZN3TmpD1Ev(ptr {{.*}} %[[TMP]])
