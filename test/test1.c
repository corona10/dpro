#include <stddef.h>
#include <stdio.h>

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
    long r = interpret(&target, 2, 3, 5);
    printf("Received: %ld\n", r);

    long r_expected = target(3, 5);
    printf("Expected: %ld\n", r_expected);

    return 0;
}
