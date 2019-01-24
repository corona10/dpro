#include "Python.h"

#include "interp.h"

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
    return (PyObject*)interpret(&_pytest3_target, 1, args);
    //Py_RETURN_NONE;
    //return PyLong_FromLong(0);
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
    initializeInterpreter("python/test/pytest3.c.ll");
    initializeInterpreter("python/cpython_ll");

    PyObject *m;

    m = PyModule_Create(&pytest3module);
    if (m == NULL)
        return NULL;

    return m;
}
