#ifndef _DCOP_INTERP_H
#define _DCOP_INTERP_H

#ifdef __cplusplus
extern "C" {
#endif

void loadBitcode(const char* llvm_filename);

struct JitTarget {
    void* target_function;
    int num_args;

    void* jitted_trace;
};

struct JitTarget* createJitTarget(void* target_function, int num_args);
long _runJitTarget(struct JitTarget* target, ...);

inline long runJitTarget0(struct JitTarget* target) {
    if (target->jitted_trace)
        return ((long (*)())target->jitted_trace)();
    return _runJitTarget(target);
}

inline long runJitTarget1(struct JitTarget* target, long arg0) {
    if (target->jitted_trace)
        return ((long (*)(long))target->jitted_trace)(arg0);
    return _runJitTarget(target, arg0);
}

inline long runJitTarget2(struct JitTarget* target, long arg0, long arg1) {
    if (target->jitted_trace)
        return ((long (*)(long, long))target->jitted_trace)(arg0, arg1);
    return _runJitTarget(target, arg0, arg1);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
