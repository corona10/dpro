#include <time.h>

#include "Python.h"

#include "interp.h"

JitTarget* jit_target;

PyObject*
_pytest3_target(PyObject* args) {
    long x, y;
    if (!PyArg_ParseTuple(args, "ll", &x, &y))
        return NULL;

    return PyLong_FromLong(x + y);
}

static PyObject *
pytest3_test(PyObject *self, PyObject *args)
{
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_REALTIME, &start);
    long interpreted = _runJitTarget(jit_target, args);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Interpreted: %ld %ldns\n", interpreted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted = _runJitTarget(jit_target, args);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted     : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted2 = ((long (*)(long))jit_target->jitted_trace)((long)args);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted2    : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long jitted3 = runJitTarget1(jit_target, (long)args);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Jitted3    : %ld %ldns\n", jitted, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    clock_gettime(CLOCK_REALTIME, &start);
    long expected = (long)_pytest3_target(args);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("Expected   : %ld %ldns\n", expected, 1000000000 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec);

    return (PyObject*)jitted;
}

static PyMethodDef pytest3Methods[] = {
    {"test",  pytest3_test, METH_VARARGS,
     "Run test"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef pytest3module = {
    PyModuleDef_HEAD_INIT,
    "pytest3",   /* name of module */
    NULL, /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    pytest3Methods
};

PyMODINIT_FUNC
PyInit_pytest3(void)
{
    loadBitcode("python/test/pytest3.c.ll");
    loadBitcode("python/cpython_ll");

    jit_target = createJitTarget(&_pytest3_target, 1);

    PyObject *m;

    m = PyModule_Create(&pytest3module);
    if (m == NULL)
        return NULL;

    return m;
}
