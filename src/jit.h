#ifndef _PYSTOL_JIT_H
#define _PYSTOL_JIT_H

#include <list>
#include <memory>
#include <vector>

#include "llvm/Transforms/Utils/ValueMapper.h" // For ValueToValueMapTy

namespace llvm {
class BasicBlock;
class Constant;
class Function;
class GlobalVariable;
class Instruction;
class LLVMContext;
class Module;
class Value;
class Type;
}

namespace pystol {

class LLVMJitCompiler;
class LLVMCompiler {
private:
    std::unique_ptr<LLVMJitCompiler> jit;
public:
    LLVMCompiler();
    ~LLVMCompiler();

    void* compile(std::unique_ptr<llvm::Module> module, std::string funcname);
};

class LLVMJit {
private:
    llvm::LLVMContext* llvm_context;
    LLVMCompiler* compiler;

    std::unique_ptr<llvm::Module> module;
    llvm::Function* func;
    llvm::BasicBlock* cur_bb;

    std::list<llvm::ValueToValueMapTy> vmaps;

    static int num_functions;
    static std::string getUniqueFunctionName(std::string nameprefix);

    llvm::Constant* cloneConstant(const llvm::Constant* constant);

    void optimizeFunc();

public:
    LLVMJit(const llvm::Function* orig_function,
            llvm::LLVMContext* llvm_context, LLVMCompiler* compiler);

    void startScope();
    void endScope();

    typedef llvm::Value* Value;

    Value arg(int argnum);
    Value constantInt(long value, llvm::Type* type);
    Value alloca(llvm::Type* type);

    Value bitcast(Value v, llvm::Type* type);
    Value gepInBounds(Value v, std::vector<int> indices);
    void store(Value v, Value ptr);

    llvm::Constant* addGlobal(const llvm::GlobalVariable* gv);
    llvm::Function* addFunction(const llvm::Function* func);

    void map(const llvm::Value* from, llvm::Value* to);
    void map(const llvm::Value* from, const llvm::Value* to);
    Value addInst(const llvm::Instruction* inst);

    void ensureConstant(Value v, long constant);

    Value call(Value ptr, const std::vector<Value>& args);

    void* finish(Value retval);
};

}

#endif
