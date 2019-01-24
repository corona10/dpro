#include "Python.h"

#include "interp.h"

PyObject*
_pytest1_target() {
    Py_RETURN_NONE;
}

static PyObject *
pytest1_test(PyObject *self, PyObject *args)
{
    long x, y;
    if (!PyArg_ParseTuple(args, "ll", &x, &y))
        return NULL;

    return (PyObject*)interpret(&_pytest1_target, 0);
    //Py_RETURN_NONE;
    //return PyLong_FromLong(0);
}

static PyMethodDef pytest1Methods[] = {
    {"test",  pytest1_test, METH_VARARGS,
     "Run test"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef pytest1module = {
    PyModuleDef_HEAD_INIT,
    "pytest1",   /* name of module */
    NULL, /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    pytest1Methods
};

PyMODINIT_FUNC
PyInit_pytest1(void)
{
    loadBitcode("python/test/pytest1.c.ll");
    loadBitcode("python/cpython_ll");

    PyObject *m;

    m = PyModule_Create(&pytest1module);
    if (m == NULL)
        return NULL;

    return m;
}
