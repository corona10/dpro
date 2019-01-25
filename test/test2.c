#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "interp.h"

struct O {
    int n;
} o;

int sum_va(int narg, va_list* va) {
    int r = 0;
    for (int i = 0; i < narg; i++) {
        r += va_arg(*va, int);
    }
    return r;
}

int sum_va2(int narg, va_list va) {
    int r = 0;
    for (int i = 0; i < narg; i++) {
        r += va_arg(va, int);
    }
    return r;
}

int sum(int narg, ...) {
    va_list va;

    va_start(va, narg);
    int r = sum_va(narg, &va);
    va_end(va);

    return r;
}

int sum2(int narg, ...) {
    va_list va;

    va_start(va, narg);
    int r = sum_va2(narg, va);
    va_end(va);

    return r;
}

int array[] = {1, 2, 3, 4, 5, 6};

int testArrayAccess(int x) __attribute__((noinline));
int testArrayAccess(int x) {
    return array[x];
}

void testPassPtr(int *x) __attribute__((noinline));
void testPassPtr(int *x) {
    *x = 1;
}

int testStrConstant(const char *x) __attribute__((noinline));
int testStrConstant(const char *x) {
    return x[1];
}

int switch_test(int x, int y) {
    switch (x) {
        case 1:
            return y;
        case 2:
            y += 1;
        case 3:
            break;
        default:
            return x;
    }
    return x + y;
}

struct O* lib(struct O*);

int fib(int n) {
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int target(int x, int y) {
    if (x < y)
        x += y;

    if (x <= y)
        x += testArrayAccess(x + y);

    long r = x + y;
    r += o.n;

    r += testArrayAccess(r);

    int z;
    testPassPtr(&z);
    r += z;

    r += testStrConstant("hello world");

    r += sum(2, x, y);
    r += sum2(2, y, x);
    r += sum(10, 1, 2, 3, 4, 5, 6u, 7, 8, 9L, 10);

    r += switch_test(0, y) + switch_test(1, y) + switch_test(2, y) + switch_test(3, y) + switch_test(4, y);

    r += (lib(&o) == &o);

    r += fib(6);

    return r;
}

struct O* test_lib() {
    return lib(&o);
}

int main() {
    loadBitcode("test/test2.c.ll");
    loadBitcode("test/lib.c.ll");

    JitTarget* jit_target = createJitTarget(&target, 2);

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
