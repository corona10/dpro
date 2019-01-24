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
long runJitTarget(struct JitTarget* region, ...);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
