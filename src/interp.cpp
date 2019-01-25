#include <dlfcn.h>
#include <memory>
#include <unordered_map>
#include <vector>

#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/SourceMgr.h"

#include "common.h"
#include "jit.h"

#include "interp.h"

using namespace llvm;
using namespace std;

#define VERBOSE

namespace dcop {

string findNameForAddress(void* address) {
    Dl_info info;
    int r = dladdr(address, &info);
    RELEASE_ASSERT(r != 0, "");
    RELEASE_ASSERT(info.dli_sname, "no match for %p", address);
    RELEASE_ASSERT(info.dli_saddr, "no match for %p", address);
    RELEASE_ASSERT(info.dli_saddr == address, "not exact match");

    return info.dli_sname;
}

void* findAddressForName(const string& name) {
    void* r = dlsym(nullptr, name.c_str());
    RELEASE_ASSERT(r, "'%s' not found", name.c_str());
    return r;
}

LLVMContext context;
const DataLayout* data_layout;

class BitcodeRegistry {
private:
    vector<unique_ptr<Module>> loaded_modules;
    unordered_map<string, Function*> functions;

public:
    void load(const char* filename) {
        static ExitOnError ExitOnErr;

        int len = strlen(filename);
        std::unique_ptr<MemoryBuffer> MB = ExitOnErr(
            errorOrToExpected(MemoryBuffer::getFileOrSTDIN(filename)));

        std::unique_ptr<Module> module;
        if (filename[len - 1] == 'l') {
            SMDiagnostic Err;
            bool DisableVerify = false;
            auto ModuleAndIndex = parseAssemblyFileWithIndex(
                filename, Err, context, nullptr, !DisableVerify);
            module = std::move(ModuleAndIndex.Mod);
            if (!module.get()) {
                Err.print("", errs());
                abort();
            }
        } else
            module = ExitOnErr(getLazyBitcodeModule(*MB, context));

        //outs() << *module << '\n';

        for (auto& func : *module) {
            if (!func.empty())
                functions[func.getName()] = &func;
        }

        data_layout = &module->getDataLayout();

        loaded_modules.push_back(move(module));
    }

    Function* findFunction(string name) {
        RELEASE_ASSERT(functions.count(name), "%s", name.c_str());
        return functions[name];
    }
} bitcode_registry;

const Function* functionForAddress(intptr_t address) {
    string name = findNameForAddress((void*)address);
    return bitcode_registry.findFunction(name);
}

class TraceStrategy {
public:
    bool shouldntTrace(void* addr) {
        if (addr == &printf)
            return true;
        return false;
    }
    bool shouldTraceInto(llvm::StringRef function_name) {
        if (function_name == "PyObject_Malloc")
            return false;
        if (function_name == "Py_FatalError")
            return false;
        if (function_name == "PyErr_Restore")
            return false;
        if (function_name == "PyErr_Format")
            return false;
        return true;
    }
};

class RuntimeValue {
public:
    enum Type {
        Immediate,
        Pointed,
    } type;

    long data;

    RuntimeValue() : type(Pointed), data(-1) {}
    RuntimeValue(long data) : type(Immediate), data(data) {}
    RuntimeValue(void* ptr) : type(Immediate), data((intptr_t)ptr) {}
    RuntimeValue(void* data, Type type) : type(type), data((intptr_t)data) {}

    long getData() const {
        RELEASE_ASSERT(type == Immediate, "");
        return data;
    }
};

template <typename Jit>
class Interpreter {
private:
    Jit& jit;

    class RealValue;
    class Value {
    public:
        virtual ~Value() {}
        virtual shared_ptr<RealValue> getAsRealValue(Interpreter& interpreter,
                                                     shared_ptr<Value> self)
            = 0;

        virtual shared_ptr<Value> call(Interpreter& interpreter,
                                       const vector<shared_ptr<Value>>& args,
                                       const CallInst* orig_inst) = 0;

        virtual void store(Interpreter& interpreter, shared_ptr<Value> val,
                           long size) = 0;
    };

    class RealValue : public Value {
    public:
        RuntimeValue runtime_value;
        typename Jit::Value jit_value;

        RealValue(RuntimeValue runtime_value, typename Jit::Value jit_value)
            : runtime_value(runtime_value), jit_value(jit_value) {}

        shared_ptr<RealValue> getAsRealValue(Interpreter& interpreter,
                                             shared_ptr<Value> self) {
            return static_pointer_cast<RealValue>(self);
        }

        shared_ptr<Value> call(Interpreter& interpreter,
                               const vector<shared_ptr<Value>>& args,
                               const CallInst* orig_inst) {
            long addr = interpreter.getAsConstInt(this);

            if (!TraceStrategy().shouldntTrace((void*)addr)) {
                const Function* function = functionForAddress(addr);
                RELEASE_ASSERT(function, "not a function?");

                if (TraceStrategy().shouldTraceInto(function->getName())) {
                    vector<shared_ptr<Value>> new_args;

                    interpreter.jit.startScope();

                    auto arg_it = function->arg_begin();
                    int i = 0;
                    while (arg_it != function->arg_end()) {
                        auto rarg
                            = args[i]->getAsRealValue(interpreter, args[i]);

                        if (rarg->jit_value->getType() != arg_it->getType())
                            rarg = make_shared<RealValue>(
                                rarg->runtime_value,
                                interpreter.jit.bitcast(rarg->jit_value,
                                                        arg_it->getType()));
                        new_args.push_back(rarg);

                        // TODO this is wrong:
                        interpreter.jit.map(arg_it, rarg->jit_value);

                        i++;
                        arg_it++;
                    }
                    while (i < args.size()) {
                        new_args.push_back(args[i]);
                        i++;
                    }

                    auto r = interpreter.interpret(interpreter.jit, function,
                                                   new_args);

                    interpreter.jit.endScope();

                    // Kind of a hack but maybe not really: Types don't need to
                    // perfectly align across translation units, so we might
                    // have received an object that was of a (similar but)
                    // different type.
                    if (r->jit_value
                        && r->jit_value->getType() != orig_inst->getType()) {
                        auto new_jitval = interpreter.jit.bitcast(
                            r->jit_value, orig_inst->getType());
                        r = make_shared<RealValue>(r->runtime_value,
                                                   new_jitval);
                    }

                    interpreter.jit.map(orig_inst, r->jit_value);
                    return r;
                }
            }

            vector<shared_ptr<RealValue>> real_args;
            for (auto arg : args) {
                real_args.push_back(arg->getAsRealValue(interpreter, arg));
            }

            // TODO: handle floating point stuff here
            long result;
            if (args.size() == 1) {
                auto ptr = (long (*)(long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData());
            } else if (args.size() == 2) {
                auto ptr = (long (*)(long, long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData());
            } else if (args.size() == 3) {
                auto ptr = (long (*)(long, long, long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData(),
                                real_args[2]->runtime_value.getData());
            } else if (args.size() == 4) {
                auto ptr = (long (*)(long, long, long, long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData(),
                                real_args[2]->runtime_value.getData(),
                                real_args[3]->runtime_value.getData());
            } else if (args.size() == 5) {
                auto ptr = (long (*)(long, long, long, long, long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData(),
                                real_args[2]->runtime_value.getData(),
                                real_args[3]->runtime_value.getData(),
                                real_args[4]->runtime_value.getData());
            } else if (args.size() == 6) {
                auto ptr = (long (*)(long, long, long, long, long, long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData(),
                                real_args[2]->runtime_value.getData(),
                                real_args[3]->runtime_value.getData(),
                                real_args[4]->runtime_value.getData(),
                                real_args[5]->runtime_value.getData());
            } else if (args.size() == 7) {
                auto ptr
                    = (long (*)(long, long, long, long, long, long, long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData(),
                                real_args[2]->runtime_value.getData(),
                                real_args[3]->runtime_value.getData(),
                                real_args[4]->runtime_value.getData(),
                                real_args[5]->runtime_value.getData(),
                                real_args[6]->runtime_value.getData());
            } else if (args.size() == 8) {
                auto ptr = (long (*)(long, long, long, long, long, long, long,
                                     long))addr;
                result = (*ptr)(real_args[0]->runtime_value.getData(),
                                real_args[1]->runtime_value.getData(),
                                real_args[2]->runtime_value.getData(),
                                real_args[3]->runtime_value.getData(),
                                real_args[4]->runtime_value.getData(),
                                real_args[5]->runtime_value.getData(),
                                real_args[6]->runtime_value.getData(),
                                real_args[7]->runtime_value.getData());
            } else {
                RELEASE_ASSERT(0, "%ld", args.size());
            }

            //vector<typename Jit::Value> jit_args;
            //for (auto& arg : real_args) {
                //jit_args.push_back(arg->jit_value);
            //}
            //auto jit_result = interpreter.jit.call(jit_value, jit_args);
            auto func = orig_inst->getCalledFunction();
            if (func)
                interpreter.jit.addFunction(func);
            auto jit_result = interpreter.jit.addInst(orig_inst);
            return make_shared<RealValue>(result, jit_result);
        }

        void store(Interpreter& interpreter, shared_ptr<Value> val, long size) {
            long ptr_long = interpreter.getAsInt(this);
            long val_long = interpreter.getAsInt(val);

            long loaded;
            switch (size) {
                case 4:
                    *(int*)ptr_long = val_long;
                    break;
                case 8:
                    *(long*)ptr_long = val_long;
                    break;
                default:
                    RELEASE_ASSERT(0, "unhandled size %ld", size);
            }
        }
    };

    class FakeValue : public Value {
        shared_ptr<RealValue> getAsRealValue(Interpreter& interpreter,
                                             shared_ptr<Value> self) {
            RELEASE_ASSERT(0, "not supported");
        }

        shared_ptr<Value> call(Interpreter& interpreter,
                               const vector<shared_ptr<Value>>& args,
                               const CallInst* orig_inst) {
            RELEASE_ASSERT(0, "not supported");
        }

        void store(Interpreter& interpreter, shared_ptr<Value> val, long size) {
            RELEASE_ASSERT(0, "not supported");
        }
    };

    class FakePointer : public FakeValue {
    private:
        shared_ptr<Value> pointee;

    public:
        FakePointer(shared_ptr<Value> pointee) : pointee(move(pointee)) {}

        void store(Interpreter& interpreter, shared_ptr<Value> val, long size) {
            pointee = move(val);
        }
    };

    //class VaList : public FakeValue {
    //public:
    //};

    class Intrinsic : public FakeValue {
    public:
        enum ID {
            VaStart,
            VaEnd,
        } id;

        Intrinsic(ID id) : id(id) {}

        shared_ptr<Value> call(Interpreter& interpreter,
                               const vector<shared_ptr<Value>>& args,
                               const CallInst* orig_inst) {
            if (id == VaStart) {
                auto tag = args[0]->getAsRealValue(interpreter, args[0]);
                long tag_ptr = interpreter.getAsInt(args[0]);

                auto vaargs = interpreter.getVaArgs();

                struct va_list_tag {
                    int index;
                    int unknown;
                    intptr_t* stackptr;
                    intptr_t* regptr;
                };

                auto jit_tag_val_bitcast = tag->jit_value;
                RELEASE_ASSERT(isa<BitCastInst>(jit_tag_val_bitcast), "");
                auto tag_jit_val
                    = cast<BitCastInst>(jit_tag_val_bitcast)->getOperand(0);

                va_list_tag* va = (va_list_tag*)tag_ptr;
                va->index = 0;
                interpreter.jit.store(
                    interpreter.jit.constantInt(0, Type::getInt32Ty(context)),
                    interpreter.jit.gepInBounds(tag_jit_val, { 0, 0, 0 }));

                if (vaargs.size()) {
                    int nregs = min(6, (int)vaargs.size());
                    auto allocation = interpreter.allocate(
                        ArrayType::get(Type::getInt8Ty(context), nregs * 8));
                    va->regptr = (intptr_t*)allocation->runtime_value.getData();
                    interpreter.jit.store(
                        interpreter.jit.gepInBounds(allocation->jit_value,
                                                    { 0, 0 }),
                        interpreter.jit.gepInBounds(tag_jit_val, { 0, 0, 3 }));
                    for (int i = 0; i < nregs; i++) {
                        auto rarg
                            = vaargs[i]->getAsRealValue(interpreter, vaargs[i]);
                        va->regptr[i] = (intptr_t)rarg->runtime_value.getData();
                        interpreter.jit.store(
                            rarg->jit_value,
                            interpreter.jit.bitcast(
                                interpreter.jit.gepInBounds(
                                    allocation->jit_value, { 0, 8 * i }),
                                rarg->jit_value->getType()->getPointerTo()));
                    }
                }

                if (vaargs.size() > 6) {
                    int nstack = vaargs.size() - 6;
                    auto allocation = interpreter.allocate(
                        ArrayType::get(Type::getInt8Ty(context), nstack * 8));
                    va->stackptr
                        = (intptr_t*)allocation->runtime_value.getData();
                    interpreter.jit.store(
                        interpreter.jit.gepInBounds(allocation->jit_value,
                                                    { 0, 0 }),
                        interpreter.jit.gepInBounds(tag_jit_val, { 0, 0, 2 }));
                    for (int i = 0; i < nstack; i++) {
                        auto rarg = vaargs[i + 6]->getAsRealValue(
                            interpreter, vaargs[i + 6]);
                        va->stackptr[i]
                            = (intptr_t)rarg->runtime_value.getData();
                        interpreter.jit.store(
                            rarg->jit_value,
                            interpreter.jit.bitcast(
                                interpreter.jit.gepInBounds(
                                    allocation->jit_value, { 0, 8 * i }),
                                rarg->jit_value->getType()->getPointerTo()));
                    }
                }

                return interpreter.getVoid();
            }

            if (id == VaEnd) {
                return interpreter.getVoid();
            }

            RELEASE_ASSERT(0, "%d", id);
        }
    };

    class BlockResult {
    public:
        enum Type {
            Return,
            Branch
        } type;

        const BasicBlock* branch_to;
        shared_ptr<Value> return_value;

        BlockResult(const BasicBlock* branch_to)
            : type(Branch), branch_to(branch_to) {}
        BlockResult(shared_ptr<Value> return_value)
            : type(Return), return_value(move(return_value)) {}
    };

public:
    Interpreter(Jit& jit)
        : jit(jit) {}

    vector<unique_ptr<char>> allocations;
    shared_ptr<RealValue> allocate(long bits, Type* type) {
        RELEASE_ASSERT((bits & 7) == 0, "%ld", bits);
        long bytes = bits / 8;

        char* alloc = new char[bytes];
        allocations.push_back(unique_ptr<char>(alloc));
        return make_shared<RealValue>((intptr_t)alloc, jit.alloca(type));
    }

    shared_ptr<RealValue> allocate(Type* type) {
        return allocate(8 * data_layout->getTypeAllocSize(type), type);
    }

    unordered_map<const llvm::Value*, shared_ptr<Value>> symtable;
    void setVariable(const llvm::Value* forval, shared_ptr<Value> value) {
#ifdef VERBOSE
        //outs() << "Defining " << forval << " " << *forval << '\n';
#endif
        symtable[forval] = move(value);
    }

    long evalGepOffset(Type* ElemTy, ArrayRef<llvm::Value*> Indices) {
        long Result = 0;

        Result += getAsConstInt(getVal(Indices[0]))
                  * data_layout->getTypeAllocSize(ElemTy);

        generic_gep_type_iterator<llvm::Value* const*> GTI
            = gep_type_begin(ElemTy, Indices),
            GTE = gep_type_end(ElemTy, Indices);
        GTI++;

        for (; GTI != GTE; ++GTI) {
            llvm::Value* Idx = GTI.getOperand();
            if (StructType* STy = GTI.getStructTypeOrNull()) {
                // assert(Idx->getType()->isIntegerTy(32) && "Illegal struct
                // idx");
                // unsigned FieldNo = cast<ConstantInt>(Idx)->getZExtValue();
                unsigned FieldNo = getAsInt(getVal(Idx));

                // Get structure layout information...
                const StructLayout* Layout = data_layout->getStructLayout(STy);

                // Add in the offset, as calculated by the structure layout
                // info...
                Result += Layout->getElementOffset(FieldNo);
            } else {
                // Get the array index and the size of each array element.
                //if (int64_t arrayIdx = cast<ConstantInt>(Idx)->getSExtValue())
                if (int64_t arrayIdx = getAsInt(getVal(Idx)))
                    Result += arrayIdx
                              * data_layout->getTypeAllocSize(
                                    GTI.getIndexedType());
            }
        }

        return Result;
    }

    long getAsConstInt(RealValue* rvalue) {
        jit.ensureConstant(rvalue->jit_value, rvalue->runtime_value.getData());
        return rvalue->runtime_value.data;
    }

    long getAsConstInt(shared_ptr<Value> value) {
        auto rvalue = value->getAsRealValue(*this, value);
        return getAsConstInt(rvalue.get());
    }

    long getAsInt(RealValue* rvalue) {
        return rvalue->runtime_value.data;
    }

    long getAsInt(shared_ptr<Value> value) {
        auto rvalue = value->getAsRealValue(*this, value);
        return getAsInt(rvalue.get());
    }

    shared_ptr<RealValue> fromConstInt(long value, Type* type = nullptr) {
        if (!type)
            type = Type::getInt64Ty(context);
        return make_shared<RealValue>(RuntimeValue(value),
                                      jit.constantInt(value, type));
    }

    shared_ptr<RealValue> getVoid() {
        return make_shared<RealValue>(RuntimeValue(0L), nullptr);
    }


    vector<shared_ptr<Value>> vaargs;
    void setVaArgs(vector<shared_ptr<Value>> args) {
        vaargs = args;
    }

    vector<shared_ptr<Value>> getVaArgs() {
        return vaargs;
    }

    shared_ptr<Value> evalConstant(const Constant* val) {
        if (isa<ConstantExpr>(val)) {
            auto expr = cast<ConstantExpr>(val);
            auto opcode = expr->getOpcode();

            if (opcode == Instruction::MemoryOps::GetElementPtr) {
                long curptr = getAsInt(evalConstant(expr->getOperand(0)));
                long origptr = curptr;
                Type* tptr = expr->getOperand(0)->getType();
                RELEASE_ASSERT(tptr->isPointerTy(), "");
                Type* t = cast<PointerType>(tptr)->getElementType();

                vector<llvm::Value*> gep_operands;
                for (int i = 1; i < expr->getNumOperands(); i++) {
                    gep_operands.push_back(expr->getOperand(i));
                }
                long offset = evalGepOffset(t, gep_operands);

                RELEASE_ASSERT(offset == 0, "check this");
                RELEASE_ASSERT((offset & 7) == 0, "check this");
                offset /= 8;
                curptr += offset;
                RELEASE_ASSERT(curptr == origptr, "check this");

                auto base = expr->getOperand(0);
                // Not sure why value remapping doesn't catch this:
                if (isa<GlobalVariable>(base))
                    base = jit.addGlobal(cast<GlobalVariable>(base));
                auto jit_val
                    = ConstantExpr::getGetElementPtr(t, base, gep_operands);
                return make_shared<RealValue>(curptr, jit_val);
            }

            RELEASE_ASSERT(0, "unhandled opcode %d %s", opcode,
                           expr->getOpcodeName());
        }

        if (isa<ConstantInt>(val)) {
            auto cint = cast<ConstantInt>(val);
            RELEASE_ASSERT(cint->getBitWidth() <= 8 * sizeof(long), "");
            // TODO: not sure about this
            return fromConstInt(cint->getSExtValue(), cint->getType());
        }

        if (isa<GlobalVariable>(val)) {
            auto gv = cast<GlobalVariable>(val);

            auto jit_val = jit.addGlobal(gv);

            auto arr_type = dyn_cast<ArrayType>(
                cast<PointerType>(gv->getType())->getElementType());
            if (arr_type && arr_type->getElementType()->isIntegerTy(8)) {
                RELEASE_ASSERT(gv->isConstant(), "");

                auto initializer = gv->getInitializer();
                RELEASE_ASSERT(initializer, "");

                auto init_str = dyn_cast<ConstantDataArray>(initializer);
                RELEASE_ASSERT(init_str, "");

                StringRef s = init_str->getAsString();
                char* newdata = new char[s.size()];
                memcpy(newdata, s.data(), s.size());

                allocations.push_back(unique_ptr<char>(newdata));
                return make_shared<RealValue>((intptr_t)newdata, jit_val);
            }

            return make_shared<RealValue>(
                (intptr_t)findAddressForName(val->getName()), jit_val);
        }

        if (isa<Function>(val)) {
            auto func = cast<Function>(val);
            if (func->getName() == "llvm.va_start")
                return make_shared<Intrinsic>(Intrinsic::VaStart);
            if (func->getName() == "llvm.va_end")
                return make_shared<Intrinsic>(Intrinsic::VaEnd);
            return fromConstInt((long)findAddressForName(func->getName()));
        }

        if (isa<ConstantPointerNull>(val)) {
            return make_shared<RealValue>(
                RuntimeValue(0L),
                ConstantPointerNull::get(cast<PointerType>(val->getType())));
        }

        errs() << *val << '\n';
        RELEASE_ASSERT(0, "unhandled constant");
    }

    const BasicBlock* prev_bb = nullptr;
    void setPrevBlock(const BasicBlock* bb) {
        prev_bb = bb;
    }

    shared_ptr<Value> getVal(const llvm::Value* val) {
        if (isa<Constant>(val)) {
            return evalConstant(cast<Constant>(val));
        }

        //outs() << val << " " << *val << '\n';
        RELEASE_ASSERT(symtable.count(val), "");
        return symtable[val];
    }

    BlockResult interpret(const BasicBlock& bb) {
        for (auto& instr : bb) {
#ifdef VERBOSE
            outs() << "Interpreting " << instr << '\n';
#endif

            if (isa<CmpInst>(instr)) {
                auto& cmp = cast<CmpInst>(instr);
                auto pred = cmp.getPredicate();

                RELEASE_ASSERT(
                    data_layout->getTypeSizeInBits(cmp.getOperand(0)->getType())
                        == data_layout->getTypeSizeInBits(
                               cmp.getOperand(1)->getType()),
                    "");
                int bits = data_layout->getTypeSizeInBits(
                    cmp.getOperand(0)->getType());

                auto lhs = getVal(instr.getOperand(0));
                auto rhs = getVal(instr.getOperand(1));

                long lhs_val = getAsInt(lhs);
                long rhs_val = getAsInt(rhs);

                bool result;
                switch (pred) {
                    case CmpInst::ICMP_ULT:
                        switch (bits) {
                            case 64:
                                result = (unsigned long)lhs_val
                                         < (unsigned long)rhs_val;
                                break;
                            case 32:
                                result = (unsigned int)lhs_val
                                         < (unsigned int)rhs_val;
                                break;
                            default:
                                RELEASE_ASSERT(0, "unhandled size %d", bits);
                        }
                        break;
                    case CmpInst::ICMP_SLT:
                        switch (bits) {
                            case 64:
                                result = (long)lhs_val < (long)rhs_val;
                                break;
                            case 32:
                                result = (int)lhs_val < (int)rhs_val;
                                break;
                            default:
                                RELEASE_ASSERT(0, "unhandled size %d", bits);
                        }
                        break;
                    case CmpInst::ICMP_UGT:
                        switch (bits) {
                            case 64:
                                result = (unsigned long)lhs_val
                                         > (unsigned long)rhs_val;
                                break;
                            case 32:
                                result = (unsigned int)lhs_val
                                         > (unsigned int)rhs_val;
                                break;
                            default:
                                RELEASE_ASSERT(0, "unhandled size %d", bits);
                        }
                        break;
                    case CmpInst::ICMP_SGT:
                        switch (bits) {
                            case 64:
                                result = (long)lhs_val > (long)rhs_val;
                                break;
                            case 32:
                                result = (int)lhs_val > (int)rhs_val;
                                break;
                            default:
                                RELEASE_ASSERT(0, "unhandled size %d", bits);
                        }
                        break;
                    case CmpInst::ICMP_EQ:
                        switch (bits) {
                            case 64:
                                result = (long)lhs_val == (long)rhs_val;
                                break;
                            case 32:
                                result = (int)lhs_val == (int)rhs_val;
                                break;
                            case 8:
                                result = (char)lhs_val == (char)rhs_val;
                                break;
                            default:
                                RELEASE_ASSERT(0, "unhandled size %d", bits);
                        }
                        break;
                    case CmpInst::ICMP_NE:
                        switch (bits) {
                            case 64:
                                result = (long)lhs_val != (long)rhs_val;
                                break;
                            case 32:
                                result = (int)lhs_val != (int)rhs_val;
                                break;
                            case 8:
                                result = (char)lhs_val != (char)rhs_val;
                                break;
                            default:
                                RELEASE_ASSERT(0, "unhandled size %d", bits);
                        }
                        break;
                    default:
                        RELEASE_ASSERT(0, "unhandled predicate %d", pred);
                };

                typename Jit::Value jit_val = jit.addInst(&instr);
                setVariable(&instr, make_shared<RealValue>(result, jit_val));
                continue;
            }

            if (isa<BinaryOperator>(instr)) {
                const BinaryOperator& binop = cast<BinaryOperator>(instr);
                auto opcode = binop.getOpcode();

                auto lhs = getVal(instr.getOperand(0));
                auto rhs = getVal(instr.getOperand(1));

                //auto rlhs = lhs->getAsRealValue(*this, lhs);
                //auto rrhs = rhs->getAsRealValue(*this, rhs);
                long lhs_val = getAsInt(lhs);
                long rhs_val = getAsInt(rhs);

                int lbits = data_layout->getTypeSizeInBits(
                    instr.getOperand(0)->getType());

                long rval;
                switch (opcode) {
                case BinaryOperator::Add:
                    rval = lhs_val + rhs_val;
                    break;
                case BinaryOperator::Sub:
                    rval = lhs_val - rhs_val;
                    break;
                case BinaryOperator::Mul:
                    rval = lhs_val * rhs_val;
                    break;
                case BinaryOperator::And:
                    rval = lhs_val & rhs_val;
                    break;
                case BinaryOperator::Or:
                    rval = lhs_val | rhs_val;
                    break;
                case BinaryOperator::Shl:
                    rval = lhs_val << rhs_val;
                    break;
                case BinaryOperator::AShr:
                    switch (lbits) {
                    case 64:
                        rval = (unsigned long)lhs_val >> rhs_val;
                        break;
                    default:
                        RELEASE_ASSERT(0, "%d", lbits);
                    }
                    break;
                default:
                    RELEASE_ASSERT(0, "Unhandled binop code %d", opcode);
                }

                typename Jit::Value jit_val = jit.addInst(&instr);
                setVariable(&instr, make_shared<RealValue>(rval, jit_val));
                continue;
            }

            if (isa<SelectInst>(instr)) {
                auto& select = cast<SelectInst>(instr);

                auto cond = getVal(select.getCondition());
                long cond_val = getAsConstInt(cond);

                const llvm::Value* v;
                if (cond_val)
                    v = select.getTrueValue();
                else
                    v = select.getFalseValue();

                shared_ptr<Value> result = getVal(v);
                jit.map(&instr, v);
                setVariable(&instr, result);
                continue;
            }

            if (isa<GetElementPtrInst>(instr)) {
                long curptr = getAsInt(getVal(instr.getOperand(0)));
                long origptr = curptr;
                Type* tptr = instr.getOperand(0)->getType();
                RELEASE_ASSERT(tptr->isPointerTy(), "");
                Type* t = cast<PointerType>(tptr)->getElementType();

                vector<llvm::Value*> gep_operands;
                for (int i = 1; i < instr.getNumOperands(); i++) {
                    gep_operands.push_back(instr.getOperand(i));
                }
                long offset = evalGepOffset(t, gep_operands);

                curptr += offset;

                typename Jit::Value jit_val = jit.addInst(&instr);
                setVariable(&instr, make_shared<RealValue>(curptr, jit_val));
                continue;
            }

            if (isa<LoadInst>(instr)) {
                auto& load = cast<LoadInst>(instr);

                auto pointer = getVal(load.getPointerOperand());
                long ptr_long = getAsInt(pointer);
                long size = data_layout->getTypeStoreSize(instr.getType());

                long loaded;
                switch (size) {
                case 1:
                    loaded = *(char*)ptr_long;
                    break;
                case 4:
                    loaded = *(int*)ptr_long;
                    break;
                case 8:
                    loaded = *(long*)ptr_long;
                    break;
                default:
                    RELEASE_ASSERT(0, "unhandled size %ld", size);
                }
                typename Jit::Value jit_val = jit.addInst(&instr);
                setVariable(&instr, make_shared<RealValue>(loaded, jit_val));
                continue;
            }

            if (isa<StoreInst>(instr)) {
                auto& store = cast<StoreInst>(instr);

                auto pointer = getVal(store.getPointerOperand());
                auto val = getVal(store.getValueOperand());

                long size = data_layout->getTypeStoreSize(
                    store.getValueOperand()->getType());

                pointer->store(*this, val, size);
                jit.addInst(&instr);
                continue;
            }

            if (isa<UnaryInstruction>(instr)) {
                auto& unop = cast<UnaryInstruction>(instr);
                auto opcode = unop.getOpcode();

                long from_bits = data_layout->getTypeSizeInBits(
                    instr.getOperand(0)->getType());
                long to_bits = data_layout->getTypeSizeInBits(instr.getType());

                auto operand = getVal(instr.getOperand(0));

                long op_val = getAsInt(operand);

                long rval;
                switch (opcode) {
                case UnaryInstruction::BitCast:
                    rval = op_val;
                    break;
                case UnaryInstruction::ZExt:
                    if (from_bits == 1) {
                        RELEASE_ASSERT(op_val == 0 || op_val == 1, "uh oh");
                        rval = (unsigned long)(unsigned char)op_val;
                    } else if (from_bits == 8) {
                        rval = (unsigned long)(unsigned char)op_val;
                    } else if (from_bits == 32) {
                        rval = (unsigned long)(unsigned int)op_val;
                    } else {
                        RELEASE_ASSERT(0, "%ld", from_bits);
                    }
                    break;
                case UnaryInstruction::SExt:
                    if (from_bits == 8) {
                        rval = (char)op_val;
                    } else if (from_bits == 32) {
                        rval = (int)op_val;
                    } else {
                        RELEASE_ASSERT(0, "%ld", from_bits);
                    }
                    break;
                case UnaryInstruction::Trunc:
                    rval = op_val;
                    break;
                case UnaryInstruction::Alloca: {
#if 0
                    auto arr_type = dyn_cast<ArrayType>(
                        cast<PointerType>(instr.getType())->getElementType());
                    if (arr_type) {
                        auto struct_type
                            = dyn_cast<StructType>(arr_type->getElementType());
                        if (struct_type
                            && struct_type->getName() == "struct.__va_list_tag") {
                            setVariable(&instr, make_shared<FakePointer>(
                                                    fromConstInt(0)));
                            continue;
                        }
                    }
#endif

                    auto& alloca = cast<AllocaInst>(instr);
                    auto allocation = allocate(
                        *alloca.getAllocationSizeInBits(*data_layout),
                        alloca.getType()->getElementType());
                    setVariable(&instr, allocation);
                    jit.map(&instr, allocation->jit_value);
                    continue;
                }
                default:
                    RELEASE_ASSERT(0, "Unhandled unop code %d", opcode);
                }

                typename Jit::Value jit_val = jit.addInst(&instr);
                setVariable(&instr, make_shared<RealValue>(rval, jit_val));
                continue;
            }

            if (isa<CallInst>(instr)) {
                auto& call = cast<CallInst>(instr);

                if (isa<Function>(call.getCalledValue())) {
                    auto name = call.getCalledValue()->getName();
                    if (name == "llvm.dbg.value")
                        continue;
                    if (name == "llvm.dbg.declare")
                        continue;
                    if (name == "llvm.lifetime.start.p0i8")
                        continue;
                    if (name == "llvm.lifetime.end.p0i8")
                        continue;
#if 0
                    if (name == "llvm.va_start") {
                        getVal(call.getOperand(0))
                            ->store(*this, make_shared<VaList>(), 0);
                        continue;
                    }
#endif
                }

                auto func = getVal(call.getCalledValue());

                vector<shared_ptr<Value>> args;
                for (auto& use : call.arg_operands()) {
                    args.push_back(getVal(use));
                }

                auto ret = func->call(*this, args, &call);
                setVariable(&instr, ret);
                continue;
            }

            if (isa<ReturnInst>(instr)) {
                auto retval = cast<ReturnInst>(instr).getReturnValue();
                if (!retval)
                    return BlockResult(getVoid());
                else
                    return BlockResult(getVal(retval));
            }

            if (isa<BranchInst>(instr)) {
                auto& br = cast<BranchInst>(instr);

                if (br.isUnconditional()) {
                    return BlockResult(br.getSuccessor(0));
                }

                long cond = getAsConstInt(getVal(br.getCondition()));
                RELEASE_ASSERT((unsigned long)cond <= 1, "");
                return BlockResult(br.getSuccessor(!cond));
            }

            if (isa<SwitchInst>(instr)) {
                auto& sw = cast<SwitchInst>(instr);

                long cond = getAsConstInt(getVal(sw.getCondition()));

                for (auto case_ : sw.cases()) {
                    if (cond == case_.getCaseValue()->getSExtValue())
                        return BlockResult(case_.getCaseSuccessor());
                }

                return BlockResult(sw.getDefaultDest());
            }

            if (isa<PHINode>(instr)) {
                auto& phi = cast<PHINode>(instr);

                bool found = false;
                for (int i = 0; i < phi.getNumIncomingValues(); i++) {
                    if (prev_bb == phi.getIncomingBlock(i)) {
                        auto v = phi.getIncomingValue(i);
                        setVariable(&instr, getVal(v));
                        jit.map(&instr, v);
                        found = true;
                        break;
                    }
                }
                RELEASE_ASSERT(found, "uh oh");
                continue;
            }

            errs() << instr << '\n';
            RELEASE_ASSERT(0, "Unhandled instr");
        }
        RELEASE_ASSERT(0, "No terminator??");
    }

    static shared_ptr<RealValue>
    interpret(Jit& jit, const Function* function,
              const vector<shared_ptr<Value>>& args) {
        Interpreter<Jit> interpreter(jit);

#ifdef VERBOSE
        // TODO: read the dbg metadata and print out source location
        //outs() << *function << '\n';
#endif

        bool is_variadic = function->isVarArg();
        int num_args = function->arg_size();
        vector<shared_ptr<Value>> vaargs;
        if (is_variadic) {
            RELEASE_ASSERT(args.size() >= num_args, "");
            for (int i = num_args; i < args.size(); i++)
                vaargs.push_back(args[i]);
            interpreter.setVaArgs(move(vaargs));
        } else {
            RELEASE_ASSERT(args.size() == num_args, "");
        }

        int i = 0;
        for (auto& arg : function->args()) {
            interpreter.setVariable(&arg, args[i]);
            i++;
        }

        RELEASE_ASSERT(!function->empty(), "no body??");

        const BasicBlock* bb = &function->getEntryBlock();
        while (true) {
            BlockResult r = interpreter.interpret(*bb);

            if (r.type == BlockResult::Branch) {
                interpreter.setPrevBlock(bb);
                bb = r.branch_to;
            } else {
                RELEASE_ASSERT(r.type == BlockResult::Return, "");
                return r.return_value->getAsRealValue(interpreter,
                                                      r.return_value);
            }
        }
    }

    static pair<RuntimeValue, void*>
    interpret(const Function* function, const vector<RuntimeValue>& params) {
        RELEASE_ASSERT(params.size() == function->arg_size(),
                       "not sure which to pass to this next line");
        static LLVMCompiler compiler;
        Jit jit(function, &context, &compiler);

        vector<shared_ptr<Value>> args;
        for (int i = 0; i < params.size(); i++) {
            args.push_back(make_shared<RealValue>(params[i], jit.arg(i)));
        }

        auto r = interpret(jit, function, args);

        auto function_addr = jit.finish(r->jit_value);
        jit.endScope();
        return make_pair(move(r->runtime_value), function_addr);
    }
};

pair<RuntimeValue, void*> interpret(void* function, vector<long> args) {
    string name = findNameForAddress(function);

    const Function* func = bitcode_registry.findFunction(name);

    RELEASE_ASSERT(args.size() == func->arg_size(), "");

    vector<RuntimeValue> params;
    int i = 0;
    for (auto& arg : func->args()) {
        auto type = arg.getType();
        RELEASE_ASSERT(type->isIntegerTy() || type->isPointerTy(), "");
        params.push_back(RuntimeValue(args[i]));
        i++;
    }

    auto r = Interpreter<LLVMJit>::interpret(func, params);
    llvm::outs() << "Return value: " << r.first.type << ' ' << r.first.data
                 << '\n';
    llvm::outs() << "Jitted function: " << r.second << '\n';

#if 0
    long jit_result;
    if (args.size() == 0) {
        jit_result = ((long (*)())r.second)();
    } else if (args.size() == 1) {
        jit_result = ((long (*)(long))r.second)(args[0]);
    } else if (args.size() == 2) {
        jit_result = ((long (*)(long, long))r.second)(args[0], args[1]);
    } else {
        RELEASE_ASSERT(0, "%ld", args.size());
    }

    llvm::outs() << "Jitted  : " << jit_result << '\n';
#endif

    return r;
}

} // namespace dcop

extern "C" {
void loadBitcode(const char* bitcode_filename) {
    if (llvm::sys::fs::is_directory(bitcode_filename)) {
        std::error_code ec;
        llvm::sys::fs::directory_iterator it(bitcode_filename, ec);
        RELEASE_ASSERT(!ec, "%d", ec.value());

        while (it != llvm::sys::fs::directory_iterator()) {
            loadBitcode(it->path().c_str());
            // printf("%s\n", it->path().c_str());

            it.increment(ec);
            RELEASE_ASSERT(!ec, "%d", ec.value());
        }

        return;
    }

    printf("Loading %s\n", bitcode_filename);
    dcop::bitcode_registry.load(bitcode_filename);
}

JitTarget* createJitTarget(void* function, int num_args) {
    return new JitTarget{ function, num_args, nullptr };
}

long _runJitTarget(JitTarget* target, ...) {
    va_list vl;
    va_start(vl, target);

    if (target->jitted_trace) {
        switch (target->num_args) {
        case 0:
            return ((long (*)())target->jitted_trace)();
        case 1:
            return ((long (*)(long))target->jitted_trace)(va_arg(vl, long));
        case 2:
            return ((long (*)(long, long))target->jitted_trace)(va_arg(vl, long), va_arg(vl, long));
        default:
            RELEASE_ASSERT(0, "%d", target->num_args);
        }
    }

    vector<long> args;
    for (int i = 0; i < target->num_args; i++) {
        args.push_back(va_arg(vl, long));
    }
    va_end(vl);

    auto r = dcop::interpret(target->target_function, args);
    target->jitted_trace = r.second;
    return r.first.getData();
}
}
