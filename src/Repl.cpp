#include "llvm-c/Target.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>

import Parser;
import Token;

using namespace llvm;
using namespace llvm::orc;
class ResourceTrackerManager {
  ResourceTrackerSP rt;

public:
  ResourceTrackerManager(ResourceTrackerSP rt) : rt(rt) {}
  ~ResourceTrackerManager() { ExitOnError()(rt->remove()); }
};

int main() {
  // todo: "taint" functions' removability if another function depends on it (or
  // somehow regenerate dependent functions)
  using namespace llvm;
  using namespace llvm::orc;
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();
  LLVMInitializeNativeAsmParser();
  ExitOnError ExitOnErr;

  auto jit = ExitOnErr(llvm::orc::LLJITBuilder().create());
  std::map<std::string, std::shared_ptr<ast::Prototype>> prototypes;
  std::map<std::string, std::unique_ptr<ResourceTrackerManager>> whatprovides;

  Parser p;
  for (;;) {
    CodegenContext ctx(jit.get(), &prototypes);
    std::cerr << ">>> ";

    // read
    std::unique_ptr<ast::Expr> ast;

    try {
      ast = p.parse(std::cin);
    } catch (Parser::ParseError e) {
      std::cerr << e.what() << '\n';
      std::cerr << "Errored on: " << p.next() << "\n";
      continue;
    }

    Value *code;
    try {
      code = ast->codegen(ctx);
      code->print(llvm::errs());
      llvm::errs() << "\n";
    } catch (CodegenException e) {
      std::cerr << e.what() << '\n';
      continue;
    }

    auto resource_tracker = jit->getMainJITDylib().createResourceTracker();

    auto top = dynamic_cast<ast::Function *>(ast.get());
    if (top) {
      whatprovides[top->get_name()] =
          std::make_unique<ResourceTrackerManager>(resource_tracker);
    }

    auto ts_module =
        ThreadSafeModule(std::move(ctx.module), std::move(ctx.ctx));
    ExitOnErr(jit->addIRModule(resource_tracker, std::move(ts_module)));

    // top level if the name is __anon_expr
    if (top && top->get_name() == "__anon_expr") {
      auto expr_symbol = ExitOnErr(jit->lookup("__anon_expr"));
      // assert(ExprSymbol && "Function not found");

      // get function
      double (*FP)() = expr_symbol.toPtr<double (*)()>();
      fprintf(stderr, "Evaluated to %f\n", FP());

      // remove __anon_expr's module
      whatprovides.erase(whatprovides.find("__anon_expr"));
    }

    // not toplevel so not managed by whatprovides
    if (!top)
      ExitOnErr(resource_tracker->remove());
  }
}
