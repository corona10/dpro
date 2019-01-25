#include <time.h>

#include "Python.h"

#include "interp.h"

JitTarget* jit_target;

PyObject*
_pytest2_target() {
    return PyLong_FromLong(400);
}

static PyObject *
pytest2_test(PyObject *self, PyObject *args)
{
    long x, y;
    if (!PyArg_ParseTuple(args, "ll", &x, &y))
        return NULL;

    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_REALTIME, &start);
    long interpreted = _runJitTarget(jit_target);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Interpreted: %ld %ldns\n", interpreted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted = _runJitTarget(jit_target);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted     : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted2 = ((long (*)())jit_target->jitted_trace)();
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted2    : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted3 = runJitTarget0(jit_target);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted3    : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long expected = (long)_pytest2_target();
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Expected   : %ld %ldns\n", expected, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    return (PyObject*)jitted;
}

static PyMethodDef pytest2Methods[] = {
    {"test",  pytest2_test, METH_VARARGS,
     "Run test"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef pytest2module = {
    PyModuleDef_HEAD_INIT,
    "pytest2",   /* name of module */
    NULL, /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    pytest2Methods
};

PyMODINIT_FUNC
PyInit_pytest2(void)
{
    loadBitcode("python/test/pytest2.c.ll");
    loadBitcode("python/cpython_ll");

    jit_target = createJitTarget(&_pytest2_target, 0);

    PyObject *m;

    m = PyModule_Create(&pytest2module);
    if (m == NULL)
        return NULL;

    return m;
}
