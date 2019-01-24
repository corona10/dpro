#include <cstdio>

#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar.h"

using namespace llvm;
using namespace llvm::orc;
using namespace std;

#include "common.h"

#include "jit.h"

namespace dcop {

// from llvm/examples/Kaleidoscope/include/KaleidoscopeJIT.h
class LLVMJitCompiler {
public:
  using ObjLayerT = RTDyldObjectLinkingLayer;
  using CompileLayerT = IRCompileLayer<ObjLayerT, SimpleCompiler>;

  LLVMJitCompiler()
      : Resolver(createLegacyLookupResolver(
            ES,
            [this](const std::string& Name) -> JITSymbol {
                // Adapted from
                // examples/Kaleidoscope/BuildingAJIT/Chapter4/KaleidoscopeJIT.h:
                if (auto sym = ObjectLayer.findSymbol(Name, true))
                    return sym;
                if (auto SymAddr
                    = RTDyldMemoryManager::getSymbolAddressInProcess(Name))
                    return JITSymbol(SymAddr, JITSymbolFlags::Exported);
                return nullptr;
            },
            [](Error Err) { cantFail(std::move(Err), "lookupFlags failed"); })),
        TM(EngineBuilder().selectTarget()), DL(TM->createDataLayout()),
        ObjectLayer(ES,
                    [this](VModuleKey) {
                      return ObjLayerT::Resources{
                          std::make_shared<SectionMemoryManager>(), Resolver};
                    }),
        CompileLayer(ObjectLayer, SimpleCompiler(*TM)) {
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
  }

  TargetMachine &getTargetMachine() { return *TM; }

  VModuleKey addModule(std::unique_ptr<Module> M) {
    auto K = ES.allocateVModule();
    cantFail(CompileLayer.addModule(K, std::move(M)));
    ModuleKeys.push_back(K);
    return K;
  }

  void removeModule(VModuleKey K) {
    ModuleKeys.erase(find(ModuleKeys, K));
    cantFail(CompileLayer.removeModule(K));
  }

  JITSymbol findSymbol(const std::string Name) {
    return findMangledSymbol(mangle(Name));
  }

private:
  std::string mangle(const std::string &Name) {
    std::string MangledName;
    {
      raw_string_ostream MangledNameStream(MangledName);
      Mangler::getNameWithPrefix(MangledNameStream, Name, DL);
    }
    return MangledName;
  }

  JITSymbol findMangledSymbol(const std::string &Name) {
#ifdef _WIN32
    // The symbol lookup of ObjectLinkingLayer uses the SymbolRef::SF_Exported
    // flag to decide whether a symbol will be visible or not, when we call
    // IRCompileLayer::findSymbolIn with ExportedSymbolsOnly set to true.
    //
    // But for Windows COFF objects, this flag is currently never set.
    // For a potential solution see: https://reviews.llvm.org/rL258665
    // For now, we allow non-exported symbols on Windows as a workaround.
    const bool ExportedSymbolsOnly = false;
#else
    const bool ExportedSymbolsOnly = true;
#endif

    // Search modules in reverse order: from last added to first added.
    // This is the opposite of the usual search order for dlsym, but makes more
    // sense in a REPL where we want to bind to the newest available definition.
    for (auto H : make_range(ModuleKeys.rbegin(), ModuleKeys.rend()))
      if (auto Sym = CompileLayer.findSymbolIn(H, Name, ExportedSymbolsOnly))
        return Sym;

    // If we can't find the symbol in the JIT, try looking in the host process.
    if (auto SymAddr = RTDyldMemoryManager::getSymbolAddressInProcess(Name))
      return JITSymbol(SymAddr, JITSymbolFlags::Exported);

#ifdef _WIN32
    // For Windows retry without "_" at beginning, as RTDyldMemoryManager uses
    // GetProcAddress and standard libraries like msvcrt.dll use names
    // with and without "_" (for example "_itoa" but "sin").
    if (Name.length() > 2 && Name[0] == '_')
      if (auto SymAddr =
              RTDyldMemoryManager::getSymbolAddressInProcess(Name.substr(1)))
        return JITSymbol(SymAddr, JITSymbolFlags::Exported);
#endif

    return nullptr;
  }

  ExecutionSession ES;
  std::shared_ptr<SymbolResolver> Resolver;
  std::unique_ptr<TargetMachine> TM;
  const DataLayout DL;
  ObjLayerT ObjectLayer;
  CompileLayerT CompileLayer;
  std::vector<VModuleKey> ModuleKeys;
};


LLVMCompiler::LLVMCompiler() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    jit = std::make_unique<LLVMJitCompiler>();
}

LLVMCompiler::~LLVMCompiler() {
}

void* LLVMCompiler::compile(unique_ptr<Module> module, string funcname) {
    jit->addModule(move(module));

    auto r = jit->findSymbol(funcname);
    RELEASE_ASSERT(r, "uh oh");
    ExitOnError ExitOnErr;
    return (void*)ExitOnErr(r.getAddress());
}

// From Pyston:
std::string LLVMJit::getUniqueFunctionName(string nameprefix) {
    static llvm::StringMap<int> used_module_names;
    std::string name;
    llvm::raw_string_ostream os(name);
    os << nameprefix;

    // in order to generate a unique id add the number of times we encountered
    // this name to end of the string.
    auto& times = used_module_names[os.str()];
    os << '_' << ++times;
    return os.str();
}


LLVMJit::LLVMJit(const Function* orig_function, LLVMContext* llvm_context,
                 LLVMCompiler* compiler)
    : llvm_context(llvm_context),
      compiler(compiler),
      module(new llvm::Module("module", *llvm_context)) {
    startScope();

    module->setDataLayout(orig_function->getParent()->getDataLayout());

    //FunctionType* ft = FunctionType::get(ret_type, arg_types, false /*vararg*/);
    FunctionType* ft = orig_function->getFunctionType();

    func = Function::Create(ft, Function::ExternalLinkage,
                            getUniqueFunctionName("traced"), module.get());

    cur_bb = BasicBlock::Create(*llvm_context, "", func);

    auto orig_args = orig_function->arg_begin();
    auto new_args = func->arg_begin();
    while (orig_args != orig_function->arg_end()) {
        map(orig_args, new_args);
        //new_args->setName("arg");
        orig_args++;
        new_args++;
    }
}

void LLVMJit::startScope() {
    vmaps.emplace_back();
}

void LLVMJit::endScope() {
    vmaps.pop_back();
}

Value* LLVMJit::arg(int argnum) {
    auto AI = func->arg_begin();
    while (argnum) {
        argnum--;
        AI++;
    }
    return &*AI;
}

Value* LLVMJit::constantInt(long value, Type* type) {
    return ConstantInt::get(type, value,
                            /* signed */ true);
}

Value* LLVMJit::alloca(Type* type) {
    auto r = new AllocaInst(type, 0);
    func->front().getInstList().insert(func->front().getFirstInsertionPt(), r);
    return r;
}

Value* LLVMJit::bitcast(Value v, llvm::Type* type) {
    auto r = new BitCastInst(v, type);
    cur_bb->getInstList().push_back(r);
    return r;
}

Value* LLVMJit::gepInBounds(Value v, vector<int> indices) {
    vector<llvm::Value*> llvm_indices;
    for (auto i : indices) {
        llvm_indices.push_back(
            ConstantInt::get(Type::getInt32Ty(*llvm_context), i));
    }
    auto r = GetElementPtrInst::CreateInBounds(v, llvm_indices, "");
    cur_bb->getInstList().push_back(r);
    return r;
}

void LLVMJit::store(Value v, Value ptr) {
    auto r = new StoreInst(v, ptr);
    cur_bb->getInstList().push_back(r);
}

Constant* LLVMJit::cloneConstant(const Constant* constant) {
    return MapValue(constant, vmaps.back());
}

Constant* LLVMJit::addGlobal(const GlobalVariable* gv) {
    GlobalVariable* new_gv = cast<GlobalVariable>(module->getOrInsertGlobal(
        gv->getName(), cast<PointerType>(gv->getType())->getElementType()));
    new_gv->copyAttributesFrom(gv);
    new_gv->setSection(gv->getSection());
    new_gv->setVisibility(gv->getVisibility());
    new_gv->setDSOLocal(gv->isDSOLocal());
    new_gv->setLinkage(gv->getLinkage());
    new_gv->setConstant(gv->isConstant());

    if (gv->hasAtLeastLocalUnnamedAddr()) {
        if (gv->hasInitializer()) {
            new_gv->setInitializer(cloneConstant(gv->getInitializer()));
        }
    }

    map(gv, new_gv);
    return new_gv;
}

llvm::Function* LLVMJit::addFunction(const llvm::Function* func) {
    Function* new_func = cast<Function>(module->getOrInsertFunction(
        func->getName(), cast<FunctionType>(cast<PointerType>(func->getType())
                                                ->getElementType())));
    new_func->copyAttributesFrom(func);
    new_func->setSection(func->getSection());
    new_func->setVisibility(func->getVisibility());
    new_func->setDSOLocal(func->isDSOLocal());
    new_func->setLinkage(func->getLinkage());
    map(func, new_func);
    return new_func;
}

void LLVMJit::map(const llvm::Value* from, llvm::Value* to) {
    while (vmaps.back().count(to) > 0 && to != vmaps.back()[to]) {
        to = vmaps.back()[to];
    }
    RELEASE_ASSERT(from != to, "uh oh");
    vmaps.back()[from] = to;
}

void LLVMJit::map(const llvm::Value* from, const llvm::Value* to) {
    if (isa<ConstantInt>(to)) {
        map(from,
            ConstantInt::get(to->getType(), cast<ConstantInt>(to)->getValue()));
        return;
    }
    RELEASE_ASSERT(vmaps.back().count(to) > 0, "");
    map(from, vmaps.back()[to]);
}

Value* LLVMJit::addInst(const Instruction* inst) {
    auto new_inst = inst->clone();
    cur_bb->getInstList().push_back(new_inst);

    map(inst, new_inst);
    RemapInstruction(new_inst, vmaps.back(),
                     RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
    new_inst->setMetadata("dbg", nullptr);
    outs() << "Emitted " << *new_inst << '\n';
    return new_inst;
}

void LLVMJit::ensureConstant(Value v, long constant) {
    if (isa<ConstantInt>(v))
        return;

    auto success_bb = BasicBlock::Create(*llvm_context, "", func);
    auto fail_bb = BasicBlock::Create(*llvm_context, "fail", func);

    llvm::Value* check_val;
    if (v->getType()->isIntegerTy())
        check_val = ConstantInt::get(v->getType(), constant);
    else {
        auto val_int
            = ConstantInt::get(Type::getInt64Ty(*llvm_context), constant);
        check_val = ConstantExpr::getIntToPtr(val_int, v->getType());
    }
    auto cond = new ICmpInst(*cur_bb, CmpInst::ICMP_EQ, v,
                             check_val);
    outs() << "Emitted guard " << *cond << '\n';
    BranchInst::Create(success_bb, fail_bb, cond, cur_bb);

    auto abort = module->getOrInsertFunction(
        "abort", FunctionType::get(Type::getVoidTy(*llvm_context), false));
    auto call = CallInst::Create(abort, "", fail_bb);
    call->setDoesNotReturn();
    new UnreachableInst(*llvm_context, fail_bb);

    cur_bb = success_bb;
}


Value* LLVMJit::call(Value ptr, const std::vector<Value>& args) {
    return CallInst::Create(ptr, args);
}

void LLVMJit::optimizeFunc() {
    llvm::legacy::FunctionPassManager fpm(module.get());

    //fpm.add(new DataLayoutPass());
    //fpm.add(createBasicAliasAnalysisPass());
    //fpm.add(createTypeBasedAliasAnalysisPass());

    if (0) {
        fpm.add(createInstructionCombiningPass());
        fpm.add(createReassociatePass());
        fpm.add(createGVNPass());
    } else {
        fpm.add(llvm::createEarlyCSEPass());                   // Catch trivial redundancies
        fpm.add(llvm::createJumpThreadingPass());              // Thread jumps.
        fpm.add(llvm::createCorrelatedValuePropagationPass()); // Propagate conditionals
        fpm.add(llvm::createCFGSimplificationPass());          // Merge & remove BBs
        fpm.add(llvm::createInstructionCombiningPass());       // Combine silly seq's

        fpm.add(llvm::createTailCallEliminationPass()); // Eliminate tail calls
        fpm.add(llvm::createCFGSimplificationPass());   // Merge & remove BBs
        fpm.add(llvm::createReassociatePass());         // Reassociate expressions
        fpm.add(llvm::createLoopRotatePass());          // Rotate Loop
        fpm.add(llvm::createLICMPass());                // Hoist loop invariants
        fpm.add(llvm::createLoopUnswitchPass(true /*optimize_for_size*/));
        fpm.add(llvm::createInstructionCombiningPass());
        fpm.add(llvm::createIndVarSimplifyPass()); // Canonicalize indvars
        fpm.add(llvm::createLoopIdiomPass());      // Recognize idioms like memset.
        fpm.add(llvm::createLoopDeletionPass());   // Delete dead loops

        fpm.add(llvm::createLoopUnrollPass()); // Unroll small loops

        fpm.add(llvm::createGVNPass());       // Remove redundancies
        fpm.add(llvm::createMemCpyOptPass()); // Remove memcpy / form memset
        fpm.add(llvm::createSCCPPass());      // Constant prop with SCCP

        // Run instcombine after redundancy elimination to exploit opportunities
        // opened up by them.
        fpm.add(llvm::createInstructionCombiningPass());
        fpm.add(llvm::createJumpThreadingPass()); // Thread jumps
        fpm.add(llvm::createCorrelatedValuePropagationPass());
        fpm.add(llvm::createDeadStoreEliminationPass()); // Delete dead stores

        fpm.add(llvm::createLoopRerollPass());
        // fpm.add(llvm::createSLPVectorizerPass());   // Vectorize parallel scalar chains.


        fpm.add(llvm::createAggressiveDCEPass());        // Delete dead instructions
        fpm.add(llvm::createCFGSimplificationPass());    // Merge & remove BBs
        fpm.add(llvm::createInstructionCombiningPass()); // Clean up after everything.

        // fpm.add(llvm::createBarrierNoopPass());
        // fpm.add(llvm::createLoopVectorizePass(DisableUnrollLoops, LoopVectorize));
        fpm.add(llvm::createInstructionCombiningPass());
        fpm.add(llvm::createCFGSimplificationPass());
    }

    fpm.doInitialization();

    bool changed = fpm.run(*func);
}

void* LLVMJit::finish(Value retval) {
    ReturnInst::Create(*llvm_context, retval, cur_bb);

    outs() << *module << '\n';

    RELEASE_ASSERT(!verifyFunction(*func, &errs()),
                   "function failed to verify");

    optimizeFunc();
    outs() << *module << '\n';

    RELEASE_ASSERT(!verifyFunction(*func, &errs()),
                   "function failed to verify");

    return compiler->compile(move(module), func->getName());
}



} // namespace dcop
