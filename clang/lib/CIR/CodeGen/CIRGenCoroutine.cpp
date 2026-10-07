//===----- CGCoroutine.cpp - Emit CIR Code for C++ coroutines -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of coroutines.
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Support/LLVM.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"

using namespace clang;
using namespace clang::CIRGen;

struct clang::CIRGen::CGCoroData {
  // What is the current await expression kind and how many
  // await/yield expressions were encountered so far.
  // These are used to generate pretty labels for await expressions in LLVM IR.
  cir::AwaitKind currentAwaitKind = cir::AwaitKind::Init;
  unsigned awaitNum = 0;
  unsigned yieldNum = 0;

  // Stores the __builtin_coro_id emitted in the function so that we can supply
  // it as the first argument to other builtins.
  cir::CoroIdOp coroId = nullptr;

  // Stores the result of __builtin_coro_begin call.
  mlir::Value coroBegin = nullptr;

  // How many co_return statements are in the coroutine. Used to decide whether
  // we need to add co_return; equivalent at the end of the user authored body.
  unsigned coreturnCount = 0;

  // The promise type's 'unhandled_exception' handler, if it defines one.
  Stmt *exceptionHandler = nullptr;

  // Stores the last emitted coro.free for the deallocate expressions, we use it
  // to wrap dealloc code with if(auto mem = coro.free) dealloc(mem).
  cir::CoroFreeOp lastCoroFree = nullptr;

  // A temporary bool alloca that stores whether 'await_resume' threw an
  // exception. If it did, 'true' is stored in this variable, and the coroutine
  // body must be skipped. If the promise type does not define an exception
  // handler, this is null.
  Address resumeEHVar = Address::invalid();

  // If coro.id came from the builtin, remember the expression to give better
  // diagnostic. If CoroIdExpr is nullptr, the coro.id was created by
  // EmitCoroutineBody.
  CallExpr const *coroIdExpr = nullptr;
};

// Defining these here allows to keep CGCoroData private to this file.
CIRGenFunction::CGCoroInfo::CGCoroInfo() {}
CIRGenFunction::CGCoroInfo::~CGCoroInfo() {}

namespace {
// FIXME: both GetParamRef and ParamReferenceReplacerRAII are good template
// candidates to be shared among LLVM / CIR codegen.

// Hunts for the parameter reference in the parameter copy/move declaration.
struct GetParamRef : public StmtVisitor<GetParamRef> {
public:
  DeclRefExpr *expr = nullptr;
  GetParamRef() {}
  void VisitDeclRefExpr(DeclRefExpr *e) {
    assert(expr == nullptr && "multilple declref in param move");
    expr = e;
  }
  void VisitStmt(Stmt *s) {
    for (Stmt *c : s->children()) {
      if (c)
        Visit(c);
    }
  }
};

// This class replaces references to parameters to their copies by changing
// the addresses in CGF.LocalDeclMap and restoring back the original values in
// its destructor.
struct ParamReferenceReplacerRAII {
  CIRGenFunction::DeclMapTy savedLocals;
  CIRGenFunction::DeclMapTy &localDeclMap;

  ParamReferenceReplacerRAII(CIRGenFunction::DeclMapTy &localDeclMap)
      : localDeclMap(localDeclMap) {}

  void addCopy(const DeclStmt *pm) {
    // Figure out what param it refers to.

    assert(pm->isSingleDecl());
    const VarDecl *vd = static_cast<const VarDecl *>(pm->getSingleDecl());
    const Expr *initExpr = vd->getInit();
    GetParamRef visitor;
    visitor.Visit(const_cast<Expr *>(initExpr));
    assert(visitor.expr);
    DeclRefExpr *dreOrig = visitor.expr;
    auto *pd = dreOrig->getDecl();

    auto it = localDeclMap.find(pd);
    assert(it != localDeclMap.end() && "parameter is not found");
    savedLocals.insert({pd, it->second});

    auto copyIt = localDeclMap.find(vd);
    assert(copyIt != localDeclMap.end() && "parameter copy is not found");
    it->second = copyIt->getSecond();
  }

  ~ParamReferenceReplacerRAII() {
    for (auto &&savedLocal : savedLocals) {
      localDeclMap.insert({savedLocal.first, savedLocal.second});
    }
  }
};
} // namespace

RValue CIRGenFunction::emitCoroutineFrame() {
  if (curCoro.data && curCoro.data->coroBegin) {
    return RValue::get(curCoro.data->coroBegin);
  }
  if (curAwaitSuspendWrapper.framePtr)
    return RValue::get(curAwaitSuspendWrapper.framePtr);
  cgm.errorNYI("NYI");
  return RValue();
}

static void createCoroData(CIRGenFunction &cgf,
                           CIRGenFunction::CGCoroInfo &curCoro,
                           cir::CoroIdOp coroId,
                           CallExpr const *coroIdExpr = nullptr) {

  if (curCoro.data) {
    if (curCoro.data->coroIdExpr)
      cgf.cgm.error(coroIdExpr->getBeginLoc(),
                    "only one __builtin_coro_id can be used in a function");
    else if (coroIdExpr)
      cgf.cgm.error(coroIdExpr->getBeginLoc(),
                    "__builtin_coro_id shall not be used in a C++ coroutine");
    else
      llvm_unreachable("EmitCoroutineBodyStatement called twice?");

    return;
  }

  curCoro.data = std::make_unique<CGCoroData>();
  curCoro.data->coroId = coroId;
  curCoro.data->coroIdExpr = coroIdExpr;
}

// Synthesize a pretty name for a suspend point, using the naming scheme of
// classic codegen: "init" and "final" for the implicit suspend points, and
// "await<N>" and "yield<N>" for the N-th co_await and co_yield, without the
// number for the first one. Unlike classic codegen, CIRGen also emits
// unreachable suspend expressions, which consume a number, so later suspend
// points may be numbered differently.
static SmallString<32> buildSuspendPrefixStr(CGCoroData &coro,
                                             cir::AwaitKind kind) {
  unsigned no = 0;
  StringRef name;
  switch (kind) {
  case cir::AwaitKind::Init:
    name = "init";
    break;
  case cir::AwaitKind::User:
    name = "await";
    no = ++coro.awaitNum;
    break;
  case cir::AwaitKind::Yield:
    name = "yield";
    no = ++coro.yieldNum;
    break;
  case cir::AwaitKind::Final:
    name = "final";
    break;
  }
  SmallString<32> prefix(name);
  if (no > 1)
    Twine(no).toVector(prefix);
  return prefix;
}

static mlir::LogicalResult
emitBodyAndFallthrough(CIRGenFunction &cgf, const CoroutineBodyStmt &s,
                       Stmt *body,
                       const CIRGenFunction::LexicalScope *currLexScope) {
  {
    // Like classic codegen, destroy the body's local variables, and the
    // temporaries they extend, when the body ends, before the fall-through
    // handler, the implicit `co_return;`, calls return_void(). The body stays
    // in the current lexical scope so that a co_return at its top level is
    // recorded in currLexScope.
    CIRGenFunction::RunCleanupsScope bodyScope(cgf);
    if (cgf.emitStmt(body, /*useCurrentScope=*/true).failed())
      return mlir::failure();
  }

  // Note that classic codegen checks CanFallthrough by looking into the
  // availability of the insert block which is kinda brittle and unintuitive,
  // seems to be related with how landing pads are handled.
  //
  // CIRGen handles this by checking pre-existing co_returns in the current
  // scope instead.

  // From LLVM IR Gen: const bool CanFallthrough = Builder.GetInsertBlock();
  const bool canFallthrough = !currLexScope->hasCoreturn();
  if (canFallthrough)
    if (Stmt *onFallthrough = s.getFallthroughHandler())
      if (cgf.emitStmt(onFallthrough, /*useCurrentScope=*/true).failed())
        return mlir::failure();

  return mlir::success();
}

cir::CoroIdOp CIRGenFunction::emitCoroIDBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());

  llvm::SmallVector<mlir::Value, 4> args;
  for (const Expr *arg : e->arguments())
    args.push_back(emitScalarExpr(arg));

  auto coroId = cir::CoroIdOp::create(cgm.getBuilder(), loc, args);
  createCoroData(*this, curCoro, coroId, e);
  return coroId;
}

cir::CoroAllocOp CIRGenFunction::emitCoroAllocBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  if (!curCoro.data || !curCoro.data->coroId) {
    cgm.error(e->getBeginLoc(), "this builtin expect that __builtin_coro_id has"
                                " been used earlier in this function");
    return {};
  }

  return cir::CoroAllocOp::create(
      cgm.getBuilder(), loc,
      mlir::ValueRange{curCoro.data->coroId.getResult()});
}

cir::CoroBeginOp CIRGenFunction::emitCoroBeginBuiltinCall(const CallExpr *e) {

  mlir::Location loc = getLoc(e->getBeginLoc());
  if (!curCoro.data || !curCoro.data->coroId) {
    cgm.error(e->getBeginLoc(), "this builtin expect that __builtin_coro_id has"
                                " been used earlier in this function");
    return {};
  }
  llvm::SmallVector<mlir::Value, 2> args;
  args.push_back(curCoro.data->coroId.getResult());
  for (const Expr *arg : e->arguments())
    args.push_back(emitScalarExpr(arg));

  auto coroBegin = cir::CoroBeginOp::create(cgm.getBuilder(), loc, args);
  curCoro.data->coroBegin = coroBegin;
  return coroBegin;
}

cir::CoroEndOp CIRGenFunction::emitCoroEndBuiltinCall(const CallExpr *e) {

  mlir::Location loc = getLoc(e->getBeginLoc());
  CIRGenBuilderTy &builder = cgm.getBuilder();
  llvm::SmallVector<mlir::Value, 3> args;
  for (const Expr *arg : e->arguments())
    args.push_back(emitScalarExpr(arg));
  args.push_back(cir::TokenNoneOp::create(builder, loc));
  return cir::CoroEndOp::create(builder, loc, {cgm.voidTy}, args);
}

cir::CoroFreeOp CIRGenFunction::emitCoroFreeBuiltin(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());

  if (!curCoro.data || !curCoro.data->coroId) {
    cgm.error(e->getBeginLoc(), "this builtin expect that __builtin_coro_id has"
                                " been used earlier in this function");
    return {};
  }

  auto coroFree =
      cir::CoroFreeOp::create(cgm.getBuilder(), loc,
                              mlir::ValueRange{curCoro.data->coroId.getResult(),
                                               curCoro.data->coroBegin});

  curCoro.data->lastCoroFree = coroFree;
  return coroFree;
}

cir::CoroSizeOp CIRGenFunction::emitCoroSizeBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  return cir::CoroSizeOp::create(cgm.getBuilder(), loc);
}

cir::CoroPromiseOp
CIRGenFunction::emitCoroPromiseBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());

  llvm::SmallVector<mlir::Value, 3> args;
  for (const Expr *arg : e->arguments())
    args.push_back(emitScalarExpr(arg));

  auto coroPromise = cir::CoroPromiseOp::create(cgm.getBuilder(), loc, args);
  return coroPromise;
}

cir::CoroDoneOp CIRGenFunction::emitCoroDoneBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  return cir::CoroDoneOp::create(cgm.getBuilder(), loc,
                                 emitScalarExpr(e->getArg(0)));
}

cir::CoroResumeOp CIRGenFunction::emitCoroResumeBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  return cir::CoroResumeOp::create(cgm.getBuilder(), loc,
                                   emitScalarExpr(e->getArg(0)));
}

cir::CoroDestroyOp
CIRGenFunction::emitCoroDestroyBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  return cir::CoroDestroyOp::create(cgm.getBuilder(), loc,
                                    emitScalarExpr(e->getArg(0)));
}

cir::CoroNoopOp CIRGenFunction::emitCoroNoopBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  return cir::CoroNoopOp::create(cgm.getBuilder(), loc);
}

cir::CoroSuspendOp
CIRGenFunction::emitCoroSuspendBuiltinCall(const CallExpr *e) {
  mlir::Location loc = getLoc(e->getBeginLoc());
  llvm::SmallVector<mlir::Value, 2> args;
  args.push_back(cir::TokenNoneOp::create(builder, loc));
  args.push_back(emitScalarExpr(e->getArg(0)));

  return cir::CoroSuspendOp::create(cgm.getBuilder(), loc, args);
  ;
}

static mlir::LogicalResult
coroutineBodyExceptionHelper(CIRGenFunction &cgf, const CoroutineBodyStmt &s) {

  CXXCatchStmt catchStmt(s.getBeginLoc(), /*exDecl=*/nullptr,
                         cgf.curCoro.data->exceptionHandler);
  auto *tryStmt = CXXTryStmt::Create(cgf.getContext(), s.getBeginLoc(),
                                     s.getBody(), &catchStmt);
  struct handlerEmitter final : CIRGenFunction::cxxTryBodyEmitter {
    const CoroutineBodyStmt &s;

    handlerEmitter(const CoroutineBodyStmt &s) : s(s) /*, scope(scope)*/ {}
    mlir::LogicalResult operator()(CIRGenFunction &cgf) override {
      return emitBodyAndFallthrough(cgf, s, s.getBody(), cgf.curLexScope);
    }
    ~handlerEmitter() override = default;
  } emitter{s};

  mlir::LogicalResult res = cgf.emitCXXTryStmt(*tryStmt, emitter);

  return res;
}

// Make the coro.id refer to the promise once it has been emitted. LLVM's
// coroutine passes give the promise named here a fixed offset in the frame,
// which is what __builtin_coro_promise, and so coroutine_handle::promise() and
// coroutine_handle::from_promise(), rely on.
static void setCoroIdPromise(CIRGenFunction &cgf, cir::CoroIdOp coroId,
                             Address promiseAddr) {
  // The coro.id is created before the promise, but the promise's alloca (or,
  // for a non-default alloca address space, its address space cast) has been
  // emitted into the entry block of the current lexical scope, which is the
  // function's: neither the cir.coroutine regions nor the exception
  // specification's cir.try open a scope of their own. It therefore precedes
  // the coro.id, or the op in that block containing it: in a noexcept
  // coroutine compiled with exceptions, the coro.id is in the cir.try that
  // calls std::terminate when an exception escapes the function. Opening a
  // LexicalScope for the initial_suspend region would break this.
  mlir::Value promisePtr = promiseAddr.getPointer();
  assert([&] {
    mlir::Operation *promiseDef = promisePtr.getDefiningOp();
    if (!promiseDef)
      return false;
    mlir::Operation *ancestor =
        promiseDef->getBlock()->findAncestorOpInBlock(*coroId);
    return ancestor && promiseDef->isBeforeInBlock(ancestor);
  }() && "promise address must dominate coro.id");

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(coroId);
  coroId.getPromiseMutable().assign(
      builder.createBitcast(coroId.getLoc(), promisePtr, cgf.voidPtrTy));
}

mlir::LogicalResult
CIRGenFunction::emitCoroutineBody(const CoroutineBodyStmt &s) {
  mlir::Location openCurlyLoc = getLoc(s.getBeginLoc());
  cir::ConstantOp nullPtrCst = builder.getNullPtr(voidPtrTy, openCurlyLoc);

  auto fn = mlir::cast<cir::FuncOp>(curFn);
  fn.setCoroutine(true);
  const TargetInfo &ti = cgm.getASTContext().getTargetInfo();
  unsigned newAlign = ti.getNewAlign() / ti.getCharWidth();

  cir::CoroIdOp coroId = cir::CoroIdOp::create(
      cgm.getBuilder(), openCurlyLoc,
      mlir::ValueRange{builder.getUInt32(newAlign, openCurlyLoc), nullPtrCst,
                       nullPtrCst, nullPtrCst});
  createCoroData(*this, curCoro, coroId);

  // Backend is allowed to elide memory allocations, to help it, emit
  // auto mem = coro.alloc() ? 0 : ... allocation code ...;
  cir::CoroAllocOp coroAlloc = cir::CoroAllocOp::create(
      cgm.getBuilder(), openCurlyLoc,
      mlir::ValueRange{curCoro.data->coroId.getResult()});

  // Initialize address of coroutine frame to null
  CanQualType astVoidPtrTy = cgm.getASTContext().VoidPtrTy;
  mlir::Type allocaTy = convertTypeForMem(astVoidPtrTy);
  Address coroFrame =
      createTempAlloca(allocaTy, getContext().getTypeAlignInChars(astVoidPtrTy),
                       openCurlyLoc, "__coro_frame_addr",
                       /*ArraySize=*/nullptr);

  mlir::Value storeAddr = coroFrame.getPointer();
  builder.CIRBaseBuilderTy::createStore(openCurlyLoc, nullPtrCst, storeAddr);
  mlir::LogicalResult res = mlir::success();
  cir::IfOp::create(
      builder, openCurlyLoc, coroAlloc.getResult(),
      /*withElseRegion=*/false,
      /*thenBuilder=*/[&](mlir::OpBuilder &b, mlir::Location loc) {
        mlir::Value allocatedPtr = emitScalarExpr(s.getAllocate());
        builder.CIRBaseBuilderTy::createStore(loc, allocatedPtr, storeAddr);
        // Handle allocation failure if 'ReturnStmtOnAllocFailure' was provided.
        if (Stmt *retOnAllocFailure = s.getReturnStmtOnAllocFailure()) {
          mlir::Value isPtrNull = builder.createPtrIsNull(allocatedPtr);
          assert(!cir::MissingFeatures::emitCondLikelihoodViaExpectIntrinsic());
          cir::IfOp::create(builder, loc, isPtrNull, /*withElseRegion=*/false,
                            [&](mlir::OpBuilder &b, mlir::Location loc) {
                              res = emitStmt(retOnAllocFailure,
                                             /*useCurrentScope=*/true);
                              cir::UnreachableOp::create(builder, loc);
                            });
        }
        cir::YieldOp::create(builder, loc);
      });

  if (res.failed())
    return res;

  curCoro.data->coroBegin = cir::CoroBeginOp::create(
      cgm.getBuilder(), openCurlyLoc,
      mlir::ValueRange{
          curCoro.data->coroId.getResult(),
          cir::LoadOp::create(builder, openCurlyLoc, allocaTy, storeAddr)});

  {
    assert(!cir::MissingFeatures::generateDebugInfo());
    ParamReferenceReplacerRAII paramReplacer(localDeclMap);

    //  Create mapping between parameters and copy-params for coroutine
    //  function.
    llvm::ArrayRef<const Stmt *> paramMoves = s.getParamMoves();
    assert((paramMoves.size() == 0 || (paramMoves.size() == fnArgs.size())) &&
           "ParamMoves and FnArgs should be the same size for coroutine "
           "function");
    // For zipping the arg map into debug info.
    assert(!cir::MissingFeatures::generateDebugInfo());

    // The parameter copies and the promise are destroyed with the coroutine
    // state, the promise first, before its storage is released
    // ([dcl.fct.def.coroutine]p11 and p13). A cir.cleanup.scope would end
    // their lifetime at the end of the region they are emitted in, so capture
    // their cleanups instead and emit them into the destroy region.
    //
    // FIXME: When the ramp throws, for example from a parameter copy
    // constructor, the promise constructor or get_return_object(), the objects
    // constructed so far must still be destroyed and the frame freed. That
    // needs the unwind region. The parameter copies are constructed before the
    // cir.coroutine, so their exceptions also need handling outside of it.
    assert(!cir::MissingFeatures::coroUnwindRegion());
    CapturedCleanups frameCleanups(*this, /*active=*/true);

    // Like classic CodeGen, keep the original parameters out of the coroutine
    // frame: only the ramp function uses them, to initialize the copies that
    // the coroutine body uses. A parameter that is destroyed in the callee, as
    // with `trivial_abi` or in the MSVC C++ ABI, is destroyed at the end of the
    // ramp function, after the coroutine may have been destroyed, so its
    // storage must not be in the frame. CIR does not destroy such parameters
    // in the callee yet.
    for (const ParmVarDecl *parm : fnArgs)
      if (auto parmAlloca =
              getAddrOfLocalVar(parm).getDefiningOp<cir::AllocaOp>())
        parmAlloca.setCoroOutsideFrame(true);

    // Create parameter copies. We do it before creating a promise, since an
    // evolution of coroutine TS may allow promise constructor to observe
    // parameter copies.
    for (auto *pm : paramMoves) {
      const auto *paramMove = cast<DeclStmt>(pm);
      emitAutoVarDeclWithCapturedCleanups(
          cast<VarDecl>(*paramMove->getSingleDecl()), frameCleanups);
      paramReplacer.addCopy(paramMove);
    }

    // Builds `initial_suspend`.
    auto initialSuspendBuilder = [&]() -> mlir::LogicalResult {
      emitAutoVarDeclWithCapturedCleanups(*s.getPromiseDecl(), frameCleanups);
      setCoroIdPromise(*this, coroId, getAddrOfLocalVar(s.getPromiseDecl()));
      // returnValue should be valid as long as the coroutine's return type
      // is not void. The assertion could help us to reduce the check later.
      assert(returnValue.isValid() == (bool)s.getReturnStmt());
      // Now we have the promise, initialize the GRO.
      // We need to emit `get_return_object` first. According to:
      // [dcl.fct.def.coroutine]p7
      // The call to get_return_­object is sequenced before the call to
      // initial_suspend and is invoked at most once.
      //
      // So we couldn't emit return value when we emit return statment,
      // otherwise the call to get_return_object wouldn't be in front
      // of initial_suspend.
      if (returnValue.isValid()) {
        // The call to get_return_object() is a full-expression, but Sema does
        // not wrap it in an ExprWithCleanups when its type matches the return
        // type. Destroy its temporaries here, so that their cleanups do not
        // outlive initial_suspend. Classic CodeGen leaves their cleanups on
        // the EH stack and destroys them only with the coroutine state.
        FullExprCleanupScope groScope(*this, s.getReturnValue());
        emitAnyExprToMem(s.getReturnValue(), returnValue,
                         s.getReturnValue()->getType().getQualifiers(),
                         /*isInit*/ true);
      }

      curCoro.data->currentAwaitKind = cir::AwaitKind::Init;
      curCoro.data->exceptionHandler = s.getExceptionHandler();

      if (emitStmt(s.getInitSuspendStmt(), /*useCurrentScope=*/true).failed())
        return mlir::failure();

      cir::YieldOp::create(builder, openCurlyLoc);
      return mlir::success();
    };

    // Builds `body`: the user-written coroutine code, including its
    // implicit `try { ... } catch (...) { unhandled_exception(); }`.
    auto bodyBuilder = [&]() -> mlir::LogicalResult {
      // The body's cleanups are popped where the body ends (in
      // emitBodyAndFallthrough, or in the cir.try around it), so this scope
      // is normally empty when it exits. It keeps any cleanup emitted in the
      // body region from outliving it: the captured frame cleanups must be on
      // top of the EH stack when the destroy region is built.
      RunCleanupsScope bodyRegionScope(*this);
      curCoro.data->currentAwaitKind = cir::AwaitKind::User;

      if (curCoro.data->exceptionHandler) {
        // This bit of code is supposed to do:
        //
        // if (await-resume-didnt-throw-exception) {
        //   try {
        //     coroutine-body
        //   } catch (...) {
        //     unhandled_exception();
        //   }
        // }
        //
        // IF resume couldn't have thrown an exception(await_resume is
        // noexcept), we skip the 'if'.
        //
        // Note that we've reversed the condition of the 'if' from classic
        // codegen so that we don't need an 'else' block.
        if (curCoro.data->resumeEHVar.isValid()) {
          mlir::Value shouldSkip = builder.createFlagLoad(
              openCurlyLoc, curCoro.data->resumeEHVar.getPointer());
          mlir::LogicalResult res = mlir::success();
          cir::IfOp::create(builder, openCurlyLoc, shouldSkip,
                            /*withElseRegion=*/false,
                            [&](mlir::OpBuilder &b, mlir::Location loc) {
                              res = coroutineBodyExceptionHelper(*this, s);
                              builder.createYield(openCurlyLoc);
                            });

          if (res.failed())
            return mlir::failure();

        } else if (coroutineBodyExceptionHelper(*this, s).failed()) {
          return mlir::failure();
        }
      } else if (emitBodyAndFallthrough(*this, s, s.getBody(), curLexScope)
                     .failed()) {
        return mlir::failure();
      }

      return mlir::success();
    };

    // Builds `final_suspend`: only emitted at all if the body can actually
    // reach it (an explicit co_return, or falling off the end).
    auto finalSuspendBuilder = [&]() -> mlir::LogicalResult {
      // Note that LLVM checks CanFallthrough by looking into the availability
      // of the insert block which is kinda brittle and unintuitive, seems to be
      // related with how landing pads are handled.
      //
      // CIRGen handles this by checking pre-existing co_returns in the current
      // scope instead.
      //
      // From LLVM IR Gen: const bool CanFallthrough = Builder.GetInsertBlock();
      const bool canFallthrough = curLexScope->hasCoreturn();
      const bool hasCoreturns = curCoro.data->coreturnCount > 0;
      if (canFallthrough || hasCoreturns) {
        curCoro.data->currentAwaitKind = cir::AwaitKind::Final;
        if (emitStmt(s.getFinalSuspendStmt(), /*useCurrentScope=*/true)
                .failed())
          return mlir::failure();
      }
      cir::YieldOp::create(builder, openCurlyLoc);
      return mlir::success();
    };

    // Destroy the promise and the parameter copies, then emit
    // "if (coro.free(CoroId, CoroBegin)) Deallocate; coro.dead(CoroBegin);"
    // like CallCoroDelete in classic codegen.
    auto destroyBuilder = [&]() -> mlir::LogicalResult {
      frameCleanups.emitIntoCleanupRegion(openCurlyLoc);

      Stmt *deallocate = s.getDeallocate();
      if (emitStmt(deallocate, /*useCurrentScope=*/true).failed()) {
        cgm.error(deallocate->getBeginLoc(),
                  "failed to emit coroutine deallocation expression");
        return mlir::failure();
      }

      cir::CoroFreeOp coroFree = curCoro.data->lastCoroFree;

      if (!coroFree) {
        cgm.error(deallocate->getBeginLoc(),
                  "Deallocation expression does not refer to coro.free");
        return mlir::failure();
      }
      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointAfter(coroFree);
        mlir::Value isPtrNotNull =
            builder.createPtrIsNotNull(coroFree.getResult());

        llvm::SmallVector<mlir::Operation *> opsToMove;
        mlir::Block *block = builder.getInsertionBlock();
        mlir::Block::iterator it(isPtrNotNull.getDefiningOp());

        for (++it; it != block->end(); ++it)
          opsToMove.push_back(&*it);

        auto ifOp = cir::IfOp::create(
            builder, getLoc(deallocate->getSourceRange()), isPtrNotNull,
            /*withElseRegion*/ false,
            [&](mlir::OpBuilder &builder, mlir::Location loc) {
              cir::YieldOp::create(builder, loc);
            });

        mlir::Operation *yieldOp = ifOp.getThenRegion().back().getTerminator();
        for (auto *op : opsToMove)
          op->moveBefore(yieldOp);
      }

      // Mark the end of the lifetime of the frame. This allows heap allocation
      // elision for a coroutine that is not destroyed explicitly.
      cir::CoroDeadOp::create(builder, openCurlyLoc, curCoro.data->coroBegin);

      cir::YieldOp::create(builder, openCurlyLoc);
      return mlir::success();
    };

    // Builds `exit`: coro.end(/*unwind*/ false) followed by the actual return
    // to the caller.
    auto exitBuilder = [&]() {
      cir::ConstantOp nullHandler =
          builder.getNullPtr(builder.getVoidPtrTy(), openCurlyLoc);
      cir::ConstantOp noUnwind = builder.getBool(false, openCurlyLoc);
      auto tkNone = cir::TokenNoneOp::create(builder, openCurlyLoc);
      cir::CoroEndOp::create(builder, openCurlyLoc, nullHandler, noUnwind,
                             tkNone);

      if (auto *ret = cast_or_null<ReturnStmt>(s.getReturnStmt())) {
        // Since we already emitted the return value above, so we shouldn't
        // emit it again here.
        Expr *previousRetValue = ret->getRetValue();
        ret->setRetValue(nullptr);
        if (emitStmt(ret, /*useCurrentScope=*/true).failed())
          return mlir::failure();
        mlir::Block *block = builder.getInsertionBlock();
        // emitReturnStmt() always creates a new insertion block after emitting
        // the return. That block is unreachable in this case, so erase it.
        block->erase();
        // Set the return value back. The code generator, as the AST
        // **Consumer**, shouldn't change the AST.
        ret->setRetValue(previousRetValue);
      } else {
        cir::ReturnOp::create(builder, openCurlyLoc);
      }

      return mlir::success();
    };

    cir::CoroutineOp coro = cir::CoroutineOp::create(
        builder, openCurlyLoc,
        /*initialSuspendBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (initialSuspendBuilder().failed())
            res = mlir::failure();
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (bodyBuilder().failed())
            res = mlir::failure();
        },
        /*finalSuspendBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (finalSuspendBuilder().failed())
            res = mlir::failure();
        },
        /*destroyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (destroyBuilder().failed())
            res = mlir::failure();
        },
        /*exitBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (exitBuilder().failed())
            res = mlir::failure();
        });

    if (coro && !coro.getBody().empty()) {
      mlir::Block &lastBlock = coro.getBody().back();
      mlir::Block *prevBlock = lastBlock.getPrevNode();
      if (lastBlock.empty() && lastBlock.hasNoPredecessors() && prevBlock &&
          !prevBlock->empty() && isa<cir::CoReturnOp>(prevBlock->back())) {
        // Get rid of the unreachable block that a co_return at the end of the
        // body leaves behind, as LexicalScope::cleanup() does.
        lastBlock.erase();
      } else if (!lastBlock.mightHaveTerminator()) {
        // This also terminates the empty block that a goto,
        // __builtin_unreachable() or __builtin_trap() at the end of the body
        // leaves behind.
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(&lastBlock);
        cir::YieldOp::create(builder, openCurlyLoc);
      }
    }
  }

  return res;
}

// Whether a call to a function may throw, like FunctionCanThrow in classic
// CodeGen.
static bool functionCanThrow(const FunctionDecl *d) {
  const auto *proto = d->getType()->getAs<FunctionProtoType>();
  // Without a prototype, conservatively assume that the function throws.
  if (!proto)
    return true;
  return !isNoexceptExceptionSpec(proto->getExceptionSpecType()) ||
         proto->canThrow() != CT_Cannot;
}

// Whether the evaluation of a statement may throw, like StmtCanThrow in classic
// CodeGen: whether it calls a function that may throw, or binds a temporary
// whose destructor may throw.
static bool stmtCanThrow(const Stmt *s) {
  if (const auto *ce = dyn_cast<CallExpr>(s)) {
    const FunctionDecl *callee = ce->getDirectCallee();
    // Without a direct callee, conservatively assume that the call throws.
    if (!callee || functionCanThrow(callee))
      return true;
  }

  // The destructor call of a bound temporary is not among the children.
  if (const auto *te = dyn_cast<CXXBindTemporaryExpr>(s))
    if (functionCanThrow(te->getTemporary()->getDestructor()))
      return true;

  return llvm::any_of(s->children(), [](const Stmt *child) {
    return child && stmtCanThrow(child);
  });
}

// The await_suspend call of a suspend expression is emitted into a separate
// function rather than into the coroutine itself:
//
//   auto __await_suspend_wrapper(auto &awaiter, void *frame) {
//     std::coroutine_handle<> handle(frame);
//     return awaiter.await_suspend(handle);
//   }
//
// The coroutine calls it through a cir.coro.intrinsic.await.suspend.*
// operation, which CoroSplit replaces with a direct call before it builds the
// coroutine frame. await_suspend may resume or destroy the coroutine, possibly
// on another thread, before it returns, which the control flow of the unsplit
// coroutine does not reflect. Keeping the call opaque until CoroSplit prevents
// the optimizations that run before it from moving accesses to the coroutine
// frame across it, and lets CoroSplit resume the coroutine returned by a
// handle-returning await_suspend with a tail call (symmetric transfer).
//
// The wrapper is generated here rather than when cir.await is lowered because
// its body is the suspend expression of the AST, emitted again with the
// awaiter bound to the first parameter and __builtin_coro_frame returning the
// second one. Outlining the suspend region of a cir.await instead would have
// to deal with the values and allocas of the coroutine that it uses, and with
// the cleanups of by-value await_suspend arguments.
cir::FuncOp
CIRGenFunction::generateAwaitSuspendWrapper(StringRef coroName,
                                            StringRef suspendPointName,
                                            const CoroutineSuspendExpr &s) {
  std::string funcName =
      (coroName + ".__await_suspend_wrapper__" + suspendPointName).str();

  ASTContext &ctx = getContext();
  auto *awaiterDecl =
      ImplicitParamDecl::Create(ctx, ctx.VoidPtrTy, ImplicitParamKind::Other);
  auto *frameDecl =
      ImplicitParamDecl::Create(ctx, ctx.VoidPtrTy, ImplicitParamKind::Other);
  // void, bool, or void * for a coroutine handle: Sema calls address() on a
  // handle returned by await_suspend.
  QualType returnTy = s.getSuspendExpr()->getType();

  FunctionArgList args;
  args.push_back(awaiterDecl);
  args.push_back(frameDecl);
  const CIRGenFunctionInfo &fnInfo =
      cgm.getTypes().arrangeBuiltinFunctionDeclaration(returnTy, args);
  cir::FuncType fnType = cgm.getTypes().getFunctionType(fnInfo);

  SourceLocRAIIObject fnLoc{*this, s.getSourceRange()};
  mlir::Location loc = getLoc(s.getSourceRange());
  cir::FuncOp fn =
      cgm.createCIRFunction(loc, funcName, fnType, /*funcDecl=*/nullptr);
  cgm.setInternalFunctionAttributes(GlobalDecl(), fn, fnInfo);

  mlir::UnitAttr unitAttr = builder.getUnitAttr();
  fn.setArgAttr(0, mlir::LLVM::LLVMDialect::getNonNullAttrName(), unitAttr);
  fn.setArgAttr(0, mlir::LLVM::LLVMDialect::getNoUndefAttrName(), unitAttr);
  fn.setArgAttr(1, mlir::LLVM::LLVMDialect::getNoUndefAttrName(), unitAttr);
  fn->setAttr(cir::CIRDialect::getMustProgressAttrName(), unitAttr);
  // Inline the wrapper as soon as the coroutine split has turned the
  // intrinsic into a direct call, even at -O0.
  fn.setInlineKind(cir::InlineKind::AlwaysInline);
  assert(!cir::MissingFeatures::coroAwaitSuspendWrapperProfileAttr());

  mlir::OpBuilder::InsertionGuard guard(builder);
  mlir::Block *entryBB = fn.addEntryBlock();
  SymTableScopeTy varScope(symbolTable);
  {
    LexicalScope lexScope(*this, loc, entryBB);
    startFunction(GlobalDecl(), returnTy, fn, fnType, args, s.getEndLoc(),
                  s.getBeginLoc());

    // The awaiter is passed as a void pointer. Bind the awaiter expression to
    // the object it points to.
    assert(!cir::MissingFeatures::opTBAA());
    const OpaqueValueExpr *awaiterExpr = s.getOpaqueValue();
    QualType awaiterTy = awaiterExpr->getType();
    mlir::Value awaiterPtr = builder.createBitcast(
        loc, builder.createLoad(loc, getAddrOfLocalVar(awaiterDecl)),
        builder.getPointerTo(convertTypeForMem(awaiterTy)));
    OpaqueValueMapping awaiterBinder(
        *this, awaiterExpr, makeNaturalAlignAddrLValue(awaiterPtr, awaiterTy));

    curAwaitSuspendWrapper.framePtr =
        builder.createLoad(loc, getAddrOfLocalVar(frameDecl));

    mlir::Value suspendRet = emitScalarExpr(s.getSuspendExpr());
    if (!returnTy->isVoidType()) {
      fn.setResultAttr(0, mlir::LLVM::LLVMDialect::getNoUndefAttrName(),
                       unitAttr);
      builder.createStore(loc, suspendRet, returnValue);
    }

    curAwaitSuspendWrapper.framePtr = nullptr;
    finishFunction(s.getEndLoc());
  }

  eraseEmptyAndUnusedBlocks(fn);
  return fn;
}

// Given a suspend expression which roughly looks like:
//
//   auto && x = CommonExpr();
//   if (!x.await_ready()) {
//      x.await_suspend(...); (*)
//   }
//   x.await_resume();
//
// where the result of the entire expression is the result of x.await_resume()
//
//   (*) The call is emitted into a wrapper function, see
//      generateAwaitSuspendWrapper. If x.await_suspend return type is bool, it
//      allows to veto a suspend:
//      if (x.await_suspend(...))
//        llvm_coro_suspend();
//      If it returns a coroutine handle, the coroutine suspends and the
//      coroutine that the handle refers to is resumed.
//
// This is more higher level than LLVM codegen, for that one see llvm's
// docs/Coroutines.md for more details.
namespace {
struct LValueOrRValue {
  LValue lv;
  RValue rv;
};
} // namespace

// Whether the await_resume() call of a suspend expression is wrapped in a
// try/catch that passes its exceptions to unhandled_exception(). This is only
// done for the initial suspend point, whose result is discarded, and only if
// the call or the destruction of its result may throw.
static bool isResumeExprInTryCatch(const CGCoroData &coro, cir::AwaitKind kind,
                                   const CoroutineSuspendExpr &s) {
  return coro.exceptionHandler && kind == cir::AwaitKind::Init &&
         stmtCanThrow(s.getResumeExpr());
}

// Emits the cir.await of a suspend expression. \p tmpResumeSlotAddr is set to
// a temporary allocated in \p scopeParentBlock that holds the await_resume()
// result, so that it can be reloaded after the cir.await: the scalar or complex
// value, or, if \p forLValue is set, the address of the object the returned
// reference refers to. It is left null if the result is ignored, is an
// aggregate (emitted into \p aggSlot), or is discarded by the initial suspend
// point's try/catch.
static LValueOrRValue
emitSuspendExpression(CIRGenFunction &cgf, CGCoroData &coro,
                      CoroutineSuspendExpr const &s, cir::AwaitKind kind,
                      AggValueSlot aggSlot, bool ignoreResult,
                      mlir::Block *scopeParentBlock,
                      mlir::Value &tmpResumeSlotAddr, bool forLValue) {
  [[maybe_unused]] mlir::LogicalResult awaitBuild = mlir::success();
  LValueOrRValue awaitRes;

  CIRGenFunction::OpaqueValueMapping binder =
      CIRGenFunction::OpaqueValueMapping(cgf, s.getOpaqueValue());
  CIRGenBuilderTy &builder = cgf.getBuilder();

  SmallString<32> prefix = buildSuspendPrefixStr(coro, kind);
  cir::FuncOp suspendWrapper =
      CIRGenFunction(cgf.cgm, builder)
          .generateAwaitSuspendWrapper(
              mlir::cast<cir::FuncOp>(cgf.curFn).getSymName(), prefix, s);

  [[maybe_unused]] cir::AwaitOp awaitOp = cir::AwaitOp::create(
      builder, cgf.getLoc(s.getSourceRange()), kind,
      /*readyBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        Expr *condExpr = s.getReadyExpr()->IgnoreParens();
        builder.createCondition(cgf.evaluateExprAsBool(condExpr));
      },
      /*suspendBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        // coro.save and coro.suspend are left to the lowering of cir.await;
        // only the await_suspend call is outlined here.
        assert(coro.coroBegin && "expected to be called in coroutine context");
        mlir::Value awaiter = builder.createBitcast(
            loc,
            cgf.getOrCreateOpaqueLValueMapping(s.getOpaqueValue()).getPointer(),
            cgf.voidPtrTy);
        mlir::Value frame = coro.coroBegin;
        StringRef wrapper = suspendWrapper.getSymName();

        // Like classic codegen, assume that a handle-returning await_suspend
        // may throw: the coroutine split resumes the returned coroutine right
        // after the wrapper call. The CFG flattening gives one that may throw
        // an unwind edge where it needs one, as classic codegen does with an
        // invoke.
        bool nothrow =
            s.getSuspendReturnType() !=
                CoroutineSuspendExpr::SuspendReturnType::SuspendHandle &&
            !stmtCanThrow(s.getSuspendExpr());
        switch (s.getSuspendReturnType()) {
        case CoroutineSuspendExpr::SuspendReturnType::SuspendVoid:
          cir::CoroAwaitSuspendVoidOp::create(builder, loc, awaiter, frame,
                                              wrapper, nothrow);
          break;
        case CoroutineSuspendExpr::SuspendReturnType::SuspendBool: {
          mlir::Value suspendRet = cir::CoroAwaitSuspendBoolOp::create(
              builder, loc, awaiter, frame, wrapper, nothrow);
          // Veto suspension if requested by bool returning await_suspend.
          builder.createCondition(suspendRet);
          return;
        }
        case CoroutineSuspendExpr::SuspendReturnType::SuspendHandle:
          // The coroutine split resumes the returned coroutine right after
          // the wrapper call.
          cir::CoroAwaitSuspendHandleOp::create(builder, loc, awaiter, frame,
                                                wrapper, nothrow);
          break;
        }
        // Signals the parent that execution flows to next region.
        cir::CoroSuspendPoint::create(builder, loc);
      },
      /*resumeBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        // Exception handling requires additional IR. If neither the
        // 'await_resume' call nor the destruction of its result can throw, we
        // avoid generating this additional IR.
        if (isResumeExprInTryCatch(coro, kind, s)) {
          // we are basically just emitting:
          // resumeEh = false;
          // try {
          //   resumeExpr();
          //   resumeEh = true;
          // } catch(...) {
          //   exceptionHandler();
          // }
          // Note the values of resumeEh are reversed from classic codegen,
          // simply so we can use an 'IfOp' without a 'else' later.
          ASTContext &ctx = cgf.getContext();
          SourceLocation resumeLoc = s.getResumeExpr()->getExprLoc();
          mlir::Location mlirLoc = cgf.getLoc(resumeLoc);
          coro.resumeEHVar = cgf.createTempAlloca(
              builder.getBoolTy(), ctx.getTypeAlignInChars(ctx.BoolTy), mlirLoc,
              "resume.eh");
          builder.createFlagStore(mlirLoc, false,
                                  coro.resumeEHVar.getPointer());

          CXXCatchStmt catchStmt(resumeLoc,
                                 /*exDecl=*/nullptr, coro.exceptionHandler);
          auto *tryBody =
              CompoundStmt::Create(ctx, s.getResumeExpr(), FPOptionsOverride(),
                                   resumeLoc, resumeLoc);
          CXXTryStmt *tryStmt =
              CXXTryStmt::Create(ctx, resumeLoc, tryBody, &catchStmt);

          struct resumeEmitter final : CIRGenFunction::cxxTryBodyEmitter {
            const CXXTryStmt &tryStmt;
            mlir::Location loc;
            mlir::Value resumeEHVar;
            resumeEmitter(const CXXTryStmt &tryStmt, mlir::Location loc,
                          Address resumeEHVar)
                : tryStmt(tryStmt), loc(loc),
                  resumeEHVar(resumeEHVar.getPointer()) {}

            mlir::LogicalResult operator()(CIRGenFunction &cgf) override {
              // The full-expression ends in the try block: destroy the
              // temporaries of the resume expression, such as its result,
              // before recording that the initial await_resume() completed,
              // so that an exception from their destructors skips the body.
              CIRGenFunction::RunCleanupsScope resumeCleanups(cgf);
              mlir::LogicalResult res =
                  cgf.emitStmt(tryStmt.getTryBlock(), /*useCurrentScope=*/true);
              resumeCleanups.forceCleanup();
              cgf.getBuilder().createFlagStore(loc, true, resumeEHVar);
              return res;
            }

            ~resumeEmitter() override = default;
          } emitter{*tryStmt, mlirLoc, coro.resumeEHVar};

          awaitBuild = cgf.emitCXXTryStmt(*tryStmt, emitter);
          // We are not supposed to obtain the value from init suspend
          // await_resume().
          awaitRes.rv = RValue::getIgnored();
        } else if (forLValue) {
          awaitRes.lv = cgf.emitLValue(s.getResumeExpr());
          assert(awaitRes.lv.isSimple() &&
                 "await_resume() should return a reference");
          // As for a scalar result, store the address of the result so that
          // it can be reloaded after the cir.await.
          mlir::Value addr = awaitRes.lv.getPointer();
          Address slot(cgf.emitAlloca(
                           "__coawait_resume_lval", addr.getType(), loc,
                           cgf.getPointerAlign(),
                           builder.getBestAllocaInsertPoint(scopeParentBlock)),
                       addr.getType(), cgf.getPointerAlign());
          builder.createStore(loc, addr, slot);
          tmpResumeSlotAddr = slot.getPointer();
        } else {
          awaitRes.rv =
              cgf.emitAnyExpr(s.getResumeExpr(), aggSlot, ignoreResult);
          if (!awaitRes.rv.isIgnored()) {
            RValue rv = awaitRes.rv;
            if (rv.isScalar() || rv.isComplex()) {
              mlir::Value value =
                  rv.isScalar() ? rv.getValue() : rv.getComplexValue();
              tmpResumeSlotAddr = cgf.emitAlloca(
                  "__coawait_resume_rval", value.getType(), loc,
                  CharUnits::One(),
                  builder.getBestAllocaInsertPoint(scopeParentBlock));
              // Store the rvalue so we can reload it before the promise call.
              builder.CIRBaseBuilderTy::createStore(loc, value,
                                                    tmpResumeSlotAddr);
            } else {
              assert(rv.isAggregate() && "unexpected rvalue kind");
            }
          }
        }

        // Returns control back to parent.
        cir::YieldOp::create(builder, loc);
      });

  assert(awaitBuild.succeeded() && "Should know how to codegen");
  return awaitRes;
}

static RValue emitSuspendExpr(CIRGenFunction &cgf,
                              const CoroutineSuspendExpr &e,
                              cir::AwaitKind kind, AggValueSlot aggSlot,
                              bool ignoreResult) {
  RValue rval;
  mlir::Location scopeLoc = cgf.getLoc(e.getSourceRange());

  // Since we model suspend / resume as an inner region, we must store
  // resume scalar results in a tmp alloca, and load it after we build the
  // suspend expression. An alternative way to do this would be to make
  // every region return a value when promise.return_value() is used, but
  // it's a bit awkward given that resume is the only region that actually
  // returns a value.
  mlir::Block *currEntryBlock = cgf.curLexScope->getEntryBlock();
  [[maybe_unused]] mlir::Value tmpResumeRValAddr;

  // await_resume() constructs its result in the resume region of the
  // cir.await, but a temporary result lives until the end of the
  // full-expression. If nothing else destroys the result, the
  // CXXBindTemporaryExpr around the call would push its destructor while the
  // region is being emitted, and the region would end inside the cleanup
  // scope. Instead, construct the result in a temporary that is marked as
  // externally destructed, and push its destructor after the cir.await, as for
  // a materialized temporary, so that it still runs at the end of the
  // full-expression. The initial await_resume() call in a try/catch discards
  // its result, which is destroyed in the try block.
  QualType resultTy = e.getType();
  QualType::DestructionKind dtorKind = resultTy.isDestructedType();
  bool destroyResult = dtorKind != QualType::DK_none &&
                       CIRGenFunction::hasAggregateEvaluationKind(resultTy) &&
                       !aggSlot.isExternallyDestructed() &&
                       !isResumeExprInTryCatch(*cgf.curCoro.data, kind, e);
  if (destroyResult) {
    if (aggSlot.isIgnored())
      aggSlot = cgf.createAggTemp(resultTy, scopeLoc, "agg.tmp.ensured");
    aggSlot.setExternallyDestructed();
    ignoreResult = false;
  }

  // No need to explicitly wrap this into a scope since the AST already uses a
  // ExprWithCleanups, which will wrap this into a cir.scope anyways.
  rval = emitSuspendExpression(cgf, *cgf.curCoro.data, e, kind, aggSlot,
                               ignoreResult, currEntryBlock, tmpResumeRValAddr,
                               /*forLValue*/ false)
             .rv;

  if (destroyResult)
    cgf.pushDestroy(dtorKind, aggSlot.getAddress(), resultTy);

  if (ignoreResult || rval.isIgnored())
    return rval;

  if (rval.isScalar()) {
    rval = RValue::get(cir::LoadOp::create(cgf.getBuilder(), scopeLoc,
                                           rval.getValue().getType(),
                                           tmpResumeRValAddr));
  } else if (rval.isAggregate()) {
    return rval;
  } else { // complex
    rval = RValue::getComplex(cir::LoadOp::create(
        cgf.getBuilder(), scopeLoc, rval.getComplexValue().getType(),
        tmpResumeRValAddr));
  }
  return rval;
}

RValue CIRGenFunction::emitCoawaitExpr(const CoawaitExpr &e,
                                       AggValueSlot aggSlot,
                                       bool ignoreResult) {
  return emitSuspendExpr(*this, e, curCoro.data->currentAwaitKind, aggSlot,
                         ignoreResult);
}

RValue CIRGenFunction::emitCoyieldExpr(const CoyieldExpr &e,
                                       AggValueSlot aggSlot,
                                       bool ignoreResult) {
  return emitSuspendExpr(*this, e, cir::AwaitKind::Yield, aggSlot,
                         ignoreResult);
}

// Emits a suspend expression that is a glvalue: await_resume() returns a
// reference.
static LValue emitSuspendLValue(CIRGenFunction &cgf,
                                const CoroutineSuspendExpr &e,
                                cir::AwaitKind kind) {
  assert(e.getResumeExpr()->isGLValue() &&
         "a glvalue suspend expression needs await_resume() to return a "
         "reference");

  // The resume region of the cir.await stores the address of the result in a
  // temporary in the enclosing scope, which is reloaded once the cir.await is
  // complete.
  mlir::Value tmpResumeLValAddr;
  LValueOrRValue res = emitSuspendExpression(
      cgf, *cgf.curCoro.data, e, kind, AggValueSlot::ignored(),
      /*ignoreResult=*/false, cgf.curLexScope->getEntryBlock(),
      tmpResumeLValAddr, /*forLValue=*/true);

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e.getSourceRange());
  if (!tmpResumeLValAddr) {
    // The initial await_resume() call in a try/catch discards its result, and
    // the initial suspend expression is a discarded-value expression, so there
    // is no address to return. Return an lvalue on a null placeholder address,
    // which is never dereferenced. An empty LValue, as in classic CodeGen,
    // does not work here: emitLValue(ExprWithCleanups) spills the address, and
    // Address requires a non-null pointer value.
    assert(isResumeExprInTryCatch(*cgf.curCoro.data, kind, e) &&
           "expected the address of the await_resume() result");
    mlir::Type elemTy = cgf.convertTypeForMem(e.getType());
    mlir::Value placeholderPtr =
        builder.getNullPtr(builder.getPointerTo(elemTy), loc);
    return cgf.makeAddrLValue(Address(placeholderPtr, elemTy, CharUnits::One()),
                              e.getType());
  }

  Address resultAddr = res.lv.getAddress();
  mlir::Value resultPtr =
      builder.createAlignedLoad(loc, resultAddr.getPointer().getType(),
                                tmpResumeLValAddr, cgf.getPointerAlign());
  return cgf.makeAddrLValue(resultAddr.withPointer(resultPtr), res.lv.getType(),
                            res.lv.getBaseInfo());
}

LValue CIRGenFunction::emitCoawaitLValue(const CoawaitExpr *e) {
  return emitSuspendLValue(*this, *e, curCoro.data->currentAwaitKind);
}

LValue CIRGenFunction::emitCoyieldLValue(const CoyieldExpr *e) {
  return emitSuspendLValue(*this, *e, cir::AwaitKind::Yield);
}

mlir::LogicalResult CIRGenFunction::emitCoreturnStmt(CoreturnStmt const &s) {
  ++curCoro.data->coreturnCount;

  const Expr *rv = s.getOperand();
  if (rv && rv->getType()->isVoidType() && !isa<InitListExpr>(rv)) {
    // Make sure to evaluate the non initlist expression of a co_return
    // with a void expression for side effects.
    RunCleanupsScope cleanupScope(*this);
    emitIgnoredExpr(rv);
  }

  if (emitStmt(s.getPromiseCall(), /*useCurrentScope=*/true).failed())
    return mlir::failure();
  // Terminate the block here; the cir.co_return transfers control to the
  // final suspend point. Record it in the current scope: unless a label
  // follows, the end of the scope is unreachable.
  mlir::Location loc = getLoc(s.getSourceRange());
  cir::CoReturnOp::create(builder, loc);
  curLexScope->setCoreturn();

  // As after a return statement, continue codegen in a new block. Statements
  // after the co_return are emitted into it and are reachable only through a
  // label (emitLabel reuses this block while it is empty). If it stays empty,
  // the enclosing scope erases or terminates it.
  builder.createBlock(builder.getBlock()->getParent());
  return mlir::success();
}
