# dpro
Dynamic Program ReOptimizer

dpro analyzes the behavior of other programs and dynamically reoptimizes them for their exhibited behavior.  It does this by tracing their execution, speculating that future execution will be similar to past executions, and emitting an optimized trace fragment that is specialized for that particular path through the code.

In other words, dpro is a tracing JIT for LLVM IR.

The thesis is that this will be beneficial for programs that exhibit multi-level dynamic behavior, since all the levels will be peeled away at once via the tracer.  The initial target for optimization is the Python interpreter (CPython), but other similarly-dynamic programs (such as database engines) should be able to be improved as well.

## Current status

dpro is currently an early prototype.  The LLVM IR tracer and JIT have been built, but have many unimplemented corner cases.

Importantly, dpro relies on modifying the target program to call into dpro to invoke the JIT.

## Code layout

`src/` contains the main source code for the LLVM interpreter and JIT.
`python/` contains everything concerning applying dpro to CPython.

Run `make pytest1` to build all the code and run a simple Python test.

The build process will build a fresh version of clang, so be prepared for it to take a while.
