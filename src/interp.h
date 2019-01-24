#ifndef _DCOP_INTERP_H
#define _DCOP_INTERP_H

#ifdef __cplusplus
extern "C" {
#endif

void loadBitcode(const char* llvm_filename);
long interpret(void* function, int num_args, ...);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
