#include "Python.h"

#include "interp.h"

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

    return (PyObject*)interpret(&_pytest2_target, 0);
    //Py_RETURN_NONE;
    //return PyLong_FromLong(0);
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
    initializeInterpreter("python/test/pytest2.c.ll");
    initializeInterpreter("python/cpython_ll");

    PyObject *m;

    m = PyModule_Create(&pytest2module);
    if (m == NULL)
        return NULL;

    return m;
}
