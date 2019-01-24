#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "interp.h"

struct O {
    int n;
} o;

int testStrConstant(const char *x) __attribute__((noinline));
int testStrConstant(const char *x) {
    return x[1];
}

int target(int x, int y) {
    int r = 0;

    r += testStrConstant("hello world");

    return r;
}

int main() {
    loadBitcode("test/test1.c.ll");

    struct JitTarget* jit_target = createJitTarget(&target, 2);

    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_REALTIME, &start);
    long expected = target(3, 5);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Expected   : %ld %ldns\n", expected, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long interpreted = runJitTarget(jit_target, 3, 5);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Interpreted: %ld %ldns\n", interpreted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted = runJitTarget(jit_target, 3, 5);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted     : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    return 0;
}
