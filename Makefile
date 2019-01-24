CLANG:=build/Release/llvm/bin/clang

.PHONY: all
all: build/Release/src/interp.so test/test1.c.ll python/cpython/python
	cd build/Release; ninja test1 pytest1

.PHONY: build/Release/src/interp.so
# Doesn't really depend on clang, depends on some llvm libraries
# TODO this should be added to the cmake rules
build/Release/src/interp.so: build/Release/build.ninja | $(CLANG)
	cd build/Release; ninja interp.so

llvm/tools/clang:
	ln -sf $(abspath clang) $(abspath llvm/tools/clang)

build/Release/build.ninja: | llvm/tools/clang
	mkdir -p build/Release
	cd build/Release; CC=clang CXX=clang++ cmake ../.. -G Ninja -DCMAKE_BUILD_TYPE=Release

build/PartialDebug/build.ninja:
	mkdir -p build/PartialDebug
	cd build/PartialDebug; CC=clang CXX=clang++ cmake ../.. -G Ninja -DCMAKE_BUILD_TYPE=PartialDebug

build/Debug/build.ninja:
	mkdir -p build/Debug
	cd build/Debug; CC=clang CXX=clang++ cmake ../.. -G Ninja -DCMAKE_BUILD_TYPE=Debug

$(CLANG): | build/Release/build.ninja
	cd build/Release; ninja clang

%.c.ll: %.c $(CLANG)
	$(CLANG) $< -Isrc -Ipython/cpython/Include -Ipython/cpython -emit-llvm -S -o $@ -O3 -g
.PRECIOUS: %.c.ll

python/cpython/Makefile: $(CLANG)
	cd python/cpython; REALCC=$(realpath $(CLANG)) CC=../../clang_wrapper.py ./configure
python/cpython/python: python/cpython/Makefile $(wildcard python/cpython/*/*.c)
	cd python/cpython; REALCC=$(realpath $(CLANG)) CC=../../clang_wrapper.py $(MAKE)

test1: test/test1.c.ll build/Release/build.ninja
	cd build/Release; ninja test1
	./build/Release/test/test1
dbg_test1: test/test1.c.ll build/PartialDebug/build.ninja
	cd build/PartialDebug; ninja test1
	gdb --args ./build/PartialDebug/test/test1
debug_test1: test/test1.c.ll build/Debug/build.ninja
	cd build/Debug; ninja test1
	gdb --args ./build/Debug/test/test1

test2: test/test2.c.ll test/lib.c.ll build/Release/build.ninja
	cd build/Release; ninja test2
	./build/Release/test/test2
dbg_test2: test/test2.c.ll test/lib.c.ll build/PartialDebug/build.ninja
	cd build/PartialDebug; ninja test2
	gdb --args ./build/PartialDebug/test/test2
debug_test2: test/test2.c.ll test/lib.c.ll build/Debug/build.ninja
	cd build/Debug; ninja test2
	gdb --args ./build/Debug/test/test2

%: python/test/%.c.ll python/cpython/python
	cd build/Release; ninja $(patsubst python/test/%.c.ll,%,$<)
	PYTHONPATH=build/Release/python/test python/cpython/python -c "import $(patsubst python/test/%.c.ll,%,$<); print($(patsubst python/test/%.c.ll,%,$<).test(4, 5))"
dbg_%: python/test/%.c.ll python/cpython/python
	cd build/PartialDebug; ninja $(patsubst python/test/%.c.ll,%,$<)
	PYTHONPATH=build/PartialDebug/python/test gdb --args python/cpython/python -c "import $(patsubst python/test/%.c.ll,%,$<); print($(patsubst python/test/%.c.ll,%,$<).test(4, 5))"
debug_%: python/test/%.c.ll python/cpython/python
	cd build/Debug; ninja $(patsubst python/test/%.c.ll,%,$<)
	PYTHONPATH=build/Debug/python/test gdb --args python/cpython/python -c "import $(patsubst python/test/%.c.ll,%,$<); print($(patsubst python/test/%.c.ll,%,$<).test(4, 5))"
