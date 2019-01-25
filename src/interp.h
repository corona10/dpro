#ifndef _DCOP_INTERP_H
#define _DCOP_INTERP_H

#ifdef __cplusplus
extern "C" {
#endif

void loadBitcode(const char* llvm_filename);

typedef struct _JitTarget {
    void* target_function;
    int num_args;

    void* jitted_trace;
} JitTarget;

JitTarget* createJitTarget(void* target_function, int num_args);
long _runJitTarget(JitTarget* target, ...);

inline long runJitTarget0(JitTarget* target) {
    if (target->jitted_trace)
        return ((long (*)())target->jitted_trace)();
    return _runJitTarget(target);
}

inline long runJitTarget1(JitTarget* target, long arg0) {
    if (target->jitted_trace)
        return ((long (*)(long))target->jitted_trace)(arg0);
    return _runJitTarget(target, arg0);
}

inline long runJitTarget2(JitTarget* target, long arg0, long arg1) {
    if (target->jitted_trace)
        return ((long (*)(long, long))target->jitted_trace)(arg0, arg1);
    return _runJitTarget(target, arg0, arg1);
}

inline long runJitTarget3(JitTarget* target, long arg0, long arg1, long arg2) {
    if (target->jitted_trace)
        return ((long (*)(long, long, long))target->jitted_trace)(arg0, arg1, arg2);
    return _runJitTarget(target, arg0, arg1, arg2);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
