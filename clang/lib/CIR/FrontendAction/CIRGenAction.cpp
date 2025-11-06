//===--- CIRGenAction.cpp - LLVM Code generation Frontend Action ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/FrontendAction/CIRGenAction.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/IR/Module.h"

using namespace cir;
using namespace clang;

namespace cir {

static BackendAction
getBackendActionFromOutputType(CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitCIR:
    assert(false &&
           "Unsupported output type for getBackendActionFromOutputType!");
    break; // Unreachable, but fall through to report that
  case CIRGenAction::OutputType::EmitAssembly:
    return BackendAction::Backend_EmitAssembly;
  case CIRGenAction::OutputType::EmitBC:
    return BackendAction::Backend_EmitBC;
  case CIRGenAction::OutputType::EmitLLVM:
    return BackendAction::Backend_EmitLL;
  case CIRGenAction::OutputType::EmitObj:
    return BackendAction::Backend_EmitObj;
  }
  // We should only get here if a non-enum value is passed in or we went through
  // the assert(false) case above
  llvm_unreachable("Unsupported output type!");
}

static std::unique_ptr<llvm::Module>
lowerFromCIRToLLVMIR(mlir::ModuleOp MLIRModule, llvm::LLVMContext &LLVMCtx) {
  return direct::lowerDirectlyFromCIRToLLVMIR(MLIRModule, LLVMCtx);
}

// Helper function to run CIR-to-CIR passes with or without an ASTContext
static mlir::LogicalResult runCIRPasses(mlir::ModuleOp MlirModule,
                                        mlir::MLIRContext &MlirCtx,
                                        const FrontendOptions &FEOptions,
                                        CodeGenOptions &CGO,
                                        ASTContext *AstCtx) {
  if (!FEOptions.ClangIRDisablePasses) {
    // If we have an ASTContext, use the full pass pipeline
    if (AstCtx) {
      return runCIRToCIRPasses(MlirModule, MlirCtx, *AstCtx,
                               !FEOptions.ClangIRDisableCIRVerifier,
                               CGO.OptimizationLevel > 0);
    }

    // For CIR input files without ASTContext, run a minimal pass pipeline
    mlir::PassManager PM(&MlirCtx);
    PM.addPass(mlir::createCIRCanonicalizePass());

    if (CGO.OptimizationLevel > 0)
      PM.addPass(mlir::createCIRSimplifyPass());

    PM.enableVerifier(!FEOptions.ClangIRDisableCIRVerifier);
    return PM.run(MlirModule);
  }
  return mlir::success();
}

// Helper function to handle output based on action type
static void handleCIROutput(CIRGenAction::OutputType Action,
                            mlir::ModuleOp MlirModule,
                            std::unique_ptr<raw_pwrite_stream> OS,
                            CompilerInstance &CI, CodeGenOptions &CGO,
                            const IntrusiveRefCntPtr<llvm::vfs::FileSystem> &FS,
                            ASTContext *AstCtx) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitCIR:
    if (OS && MlirModule) {
      mlir::OpPrintingFlags Flags;
      Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
      MlirModule->print(*OS, Flags);
    }
    break;
  case CIRGenAction::OutputType::EmitLLVM:
  case CIRGenAction::OutputType::EmitBC:
  case CIRGenAction::OutputType::EmitObj:
  case CIRGenAction::OutputType::EmitAssembly: {
    llvm::LLVMContext LLVMCtx;
    std::unique_ptr<llvm::Module> LLVMModule =
        lowerFromCIRToLLVMIR(MlirModule, LLVMCtx);

    if (!LLVMModule) {
      unsigned DiagID = CI.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error, "failed to lower CIR to LLVM IR");
      CI.getDiagnostics().Report(DiagID);
      return;
    }

    // Get data layout string - use ASTContext if available, otherwise from
    // target
    std::string DataLayoutStr;
    if (AstCtx) {
      DataLayoutStr = AstCtx->getTargetInfo().getDataLayoutString();
    } else {
      const TargetOptions &TargetOpts = CI.getTargetOpts();
      if (LLVMModule->getTargetTriple().empty()) {
        LLVMModule->setTargetTriple(llvm::Triple(TargetOpts.Triple));
      }
      DataLayoutStr = CI.getTarget().getDataLayoutString();
    }

    BackendAction BEAction = getBackendActionFromOutputType(Action);
    emitBackendOutput(CI, CGO, DataLayoutStr, LLVMModule.get(), BEAction, FS,
                      std::move(OS));
    break;
  }
  }
}

class CIRGenConsumer : public clang::ASTConsumer {

  virtual void anchor();

  CIRGenAction::OutputType Action;

  CompilerInstance &CI;

  std::unique_ptr<raw_pwrite_stream> OutputStream;

  ASTContext *Context{nullptr};
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  std::unique_ptr<CIRGenerator> Gen;
  const FrontendOptions &FEOptions;
  CodeGenOptions &CGO;

public:
  CIRGenConsumer(CIRGenAction::OutputType Action, CompilerInstance &CI,
                 CodeGenOptions &CGO, std::unique_ptr<raw_pwrite_stream> OS,
                 mlir::MLIRContext *MLIRCtx)
      : Action(Action), CI(CI), OutputStream(std::move(OS)),
        FS(&CI.getVirtualFileSystem()),
        Gen(std::make_unique<CIRGenerator>(CI.getDiagnostics(), std::move(FS),
                                           CI.getCodeGenOpts(), MLIRCtx)),
        FEOptions(CI.getFrontendOpts()), CGO(CGO) {}

  void Initialize(ASTContext &Ctx) override {
    assert(!Context && "initialized multiple times");
    Context = &Ctx;
    Gen->Initialize(Ctx);
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    Gen->HandleTopLevelDecl(D);
    return true;
  }

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *VD) override {
    Gen->HandleCXXStaticMemberVarInstantiation(VD);
  }

  void HandleInlineFunctionDefinition(FunctionDecl *D) override {
    Gen->HandleInlineFunctionDefinition(D);
  }

  void HandleTranslationUnit(ASTContext &C) override {
    Gen->HandleTranslationUnit(C);

    mlir::ModuleOp MlirModule = Gen->getModule();
    mlir::MLIRContext &MlirCtx = Gen->getMLIRContext();

    SourceManager &ClangSourceMgr = C.getSourceManager();
    FileID MainFileID = ClangSourceMgr.getMainFileID();
    std::unique_ptr<llvm::MemoryBuffer> FileBuf =
        llvm::MemoryBuffer::getMemBuffer(
            ClangSourceMgr.getBufferOrFake(MainFileID));
    llvm::SourceMgr MlirSourceMgr;
    MlirSourceMgr.AddNewSourceBuffer(std::move(FileBuf), llvm::SMLoc());
    mlir::SourceMgrDiagnosticHandler DiagnosticHandler(MlirSourceMgr, &MlirCtx);

    if (!FEOptions.ClangIRDisableCIRVerifier) {
      if (!Gen->verifyModule()) {
        CI.getDiagnostics().Report(
            diag::err_cir_verification_failed_pre_passes);
        llvm::report_fatal_error(
            "CIR codegen: module verification error before running CIR passes");
        return;
      }
    }

    // Run CIR-to-CIR passes
    if (runCIRPasses(MlirModule, MlirCtx, FEOptions, CGO, &C).failed()) {
      CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
      return;
    }

    // Handle output
    handleCIROutput(Action, MlirModule, std::move(OutputStream), CI, CGO, FS,
                    &C);
  }

  void HandleTagDeclDefinition(TagDecl *D) override {
    PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                   Context->getSourceManager(),
                                   "CIR generation of declaration");
    Gen->HandleTagDeclDefinition(D);
  }

  void HandleTagDeclRequiredDefinition(const TagDecl *D) override {
    Gen->HandleTagDeclRequiredDefinition(D);
  }

  void CompleteTentativeDefinition(VarDecl *D) override {
    Gen->CompleteTentativeDefinition(D);
  }

  void HandleVTable(CXXRecordDecl *RD) override { Gen->HandleVTable(RD); }
};
} // namespace cir

void CIRGenConsumer::anchor() {}

CIRGenAction::CIRGenAction(OutputType Act, mlir::MLIRContext *MLIRCtx)
    : MLIRCtx(MLIRCtx ? MLIRCtx : new mlir::MLIRContext), Action(Act) {
  // Load required dialects for CIR parsing and processing
  CIRGenerator::LoadCIRDialects(*this->MLIRCtx);
}

CIRGenAction::~CIRGenAction() { MLIRMod.release(); }

static std::unique_ptr<raw_pwrite_stream>
getOutputStream(CompilerInstance &CI, StringRef InFile,
                CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitAssembly:
    return CI.createDefaultOutputFile(false, InFile, "s");
  case CIRGenAction::OutputType::EmitCIR:
    return CI.createDefaultOutputFile(false, InFile, "cir");
  case CIRGenAction::OutputType::EmitLLVM:
    return CI.createDefaultOutputFile(false, InFile, "ll");
  case CIRGenAction::OutputType::EmitBC:
    return CI.createDefaultOutputFile(true, InFile, "bc");
  case CIRGenAction::OutputType::EmitObj:
    return CI.createDefaultOutputFile(true, InFile, "o");
  }
  llvm_unreachable("Invalid CIRGenAction::OutputType");
}

std::unique_ptr<ASTConsumer>
CIRGenAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  // If the input is a CIR file, we don't need an AST consumer since we'll
  // handle it directly in ExecuteAction(). Return a dummy consumer to avoid
  // aborting the action.
  if (getCurrentFileKind().getLanguage() == Language::CIR)
    return std::make_unique<ASTConsumer>();

  std::unique_ptr<llvm::raw_pwrite_stream> Out = CI.takeOutputStream();

  if (!Out)
    Out = getOutputStream(CI, InFile, Action);

  auto Result = std::make_unique<cir::CIRGenConsumer>(
      Action, CI, CI.getCodeGenOpts(), std::move(Out), MLIRCtx);

  return Result;
}

void EmitAssemblyAction::anchor() {}
EmitAssemblyAction::EmitAssemblyAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitAssembly, MLIRCtx) {}

void EmitCIRAction::anchor() {}
EmitCIRAction::EmitCIRAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitCIR, MLIRCtx) {}

void EmitLLVMAction::anchor() {}
EmitLLVMAction::EmitLLVMAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitLLVM, MLIRCtx) {}

void EmitBCAction::anchor() {}
EmitBCAction::EmitBCAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitBC, MLIRCtx) {}

void EmitObjAction::anchor() {}
EmitObjAction::EmitObjAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitObj, MLIRCtx) {}

mlir::OwningOpRef<mlir::ModuleOp>
CIRGenAction::loadModule(llvm::MemoryBufferRef MBRef) {
  CompilerInstance &CI = getCompilerInstance();
  llvm::SourceMgr MLIRSourceMgr;
  MLIRSourceMgr.AddNewSourceBuffer(llvm::MemoryBuffer::getMemBuffer(MBRef),
                                   llvm::SMLoc());

  // Create a diagnostic handler that will print MLIR errors to stderr
  mlir::SourceMgrDiagnosticHandler DiagHandler(MLIRSourceMgr, MLIRCtx);

  mlir::OwningOpRef<mlir::ModuleOp> Module =
      mlir::parseSourceFile<mlir::ModuleOp>(MLIRSourceMgr, MLIRCtx);

  if (!Module) {
    // MLIR diagnostics have already been emitted via DiagHandler
    // Add a summary error to clang's diagnostic system
    unsigned DiagID = CI.getDiagnostics().getCustomDiagID(
        DiagnosticsEngine::Error, "failed to parse CIR file '%0'");
    CI.getDiagnostics().Report(DiagID) << MBRef.getBufferIdentifier();
  }

  return Module;
}

void CIRGenAction::ExecuteAction() {
  // If this is a CIR file, we have to treat it specially.
  if (getCurrentFileKind().getLanguage() == Language::CIR) {
    CompilerInstance &CI = getCompilerInstance();

    SourceManager &SM = CI.getSourceManager();
    FileID FID = SM.getMainFileID();
    std::optional<llvm::MemoryBufferRef> MainFile = SM.getBufferOrNone(FID);
    if (!MainFile)
      return;

    MLIRMod = loadModule(*MainFile);
    if (!MLIRMod)
      return;

    std::unique_ptr<raw_pwrite_stream> OS =
        getOutputStream(CI, getCurrentFileOrBufferName(), Action);

    // Only EmitCIR can work without an output stream (for syntax checking)
    if (Action != OutputType::EmitCIR && !OS) {
      unsigned DiagID = CI.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error, "failed to create output stream");
      CI.getDiagnostics().Report(DiagID);
      return;
    }

    mlir::ModuleOp MlirModule = MLIRMod.get();

    // Setup diagnostic handler.
    llvm::SourceMgr MlirSourceMgr;
    MlirSourceMgr.AddNewSourceBuffer(
        llvm::MemoryBuffer::getMemBuffer(*MainFile), llvm::SMLoc());
    mlir::SourceMgrDiagnosticHandler DiagnosticHandler(MlirSourceMgr, MLIRCtx);

    // Run CIR-to-CIR passes (without ASTContext for CIR input files)
    const FrontendOptions &FEOpts = CI.getFrontendOpts();
    if (runCIRPasses(MlirModule, *MLIRCtx, FEOpts, CI.getCodeGenOpts(), nullptr)
            .failed()) {
      CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
      return;
    }

    // Handle output
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = &CI.getVirtualFileSystem();
    handleCIROutput(Action, MlirModule, std::move(OS), CI, CI.getCodeGenOpts(),
                    FS, nullptr);
    return;
  }

  // For source files, use the normal AST-based path.
  this->ASTFrontendAction::ExecuteAction();
}
