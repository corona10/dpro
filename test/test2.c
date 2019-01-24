#include <stddef.h>
#include <stdio.h>

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
    initializeInterpreter("test/test2.c.ll");
    initializeInterpreter("test/lib.c.ll");
    interpret(&test_lib, 0);

    long r = interpret(&target, 2, 3, 5);
    printf("Received: %ld\n", r);

    long r_expected = target(3, 5);
    printf("Expected: %ld\n", r_expected);

    return 0;
}
