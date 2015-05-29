#include <Python.h>

#include <cassert>
#include <string>
#include <vector>

#include "slapo_py_update_hook.h"
#include "cc_py_obj.h"

using std::string;
using std::vector;

namespace slapo_py_update_hook {
namespace {

CCPyObj traceback_mod;

class Counter {
  public:
    explicit Counter(int &counter) : counter_{++counter} {}
    ~Counter() { --counter_; }

  private:
    int &counter_;
};

}  // anonymous namespace

void init_cc_py_obj() {
    traceback_mod = CCPyObj::checked_steal(PyImport_ImportModule("traceback"));
}

//
// CCPyObj
//

CCPyObj::CCPyObj() : obj_{nullptr} {}
CCPyObj::CCPyObj(long value) : obj_{PyInt_FromLong(value)} {}
CCPyObj::CCPyObj(const char *str) : obj_{PyString_FromString(str)} {}
CCPyObj::CCPyObj(const string &str)
    : obj_{PyString_FromStringAndSize(str.data(), str.size())} {}
CCPyObj::CCPyObj(const CCPyObj &other) : obj_{other.obj_} { Py_XINCREF(obj_); }
CCPyObj::~CCPyObj() { Py_XDECREF(obj_); }

CCPyObj &CCPyObj::operator=(const CCPyObj &other) {
    Py_XDECREF(obj_);
    obj_ = other.obj_;
    Py_XINCREF(obj_);
    return *this;
}

// static
CCPyObj CCPyObj::unchecked_borrow(PyObject *obj) {
    Py_XINCREF(obj);
    return unchecked_steal(obj);
}
CCPyObj CCPyObj::checked_borrow(PyObject *obj) {
    Py_XINCREF(obj);
    return checked_steal(obj);
}
// static
CCPyObj CCPyObj::unchecked_steal(PyObject *obj) {
    CCPyObj result;
    result.obj_ = obj;
    return result;
}
CCPyObj CCPyObj::checked_steal(PyObject *obj) {
    CCPyObj result;
    result.obj_ = obj;
    maybe_throw(!obj);
    return result;
}

PyObject *CCPyObj::ref() { return obj_; }
PyObject *CCPyObj::new_ref() {
    Py_XINCREF(obj_);
    return obj_;
}

CCPyObj::operator long() const {
    long result;
    if (!obj_) {
        throw PyError{"Cannot cast nullptr to long"};
    } else if (PyLong_Check(obj_)) {
        result = PyLong_AsLong(obj_);
    } else if (PyInt_Check(obj_)) {
        result = PyInt_AsLong(obj_);
    } else {
        CCPyObj repr = checked_steal(PyObject_Repr(obj_));
        throw PyError{"Cannot cast " + static_cast<string>(repr) + " to long"};
    }
    maybe_throw(result == -1 && PyErr_Occurred());
    return result;
}

CCPyObj::operator string() const {
    if (!obj_) {
        throw PyError{"Cannot cast nullptr to string"};
    } else if (!PyString_Check(obj_)) {
        CCPyObj repr = checked_steal(PyObject_Repr(obj_));
        throw PyError{"Cannot cast " + static_cast<string>(repr) + " to string"};
    }
    char *exc_str_data = nullptr;
    Py_ssize_t exc_str_len = 0;
    int result = PyString_AsStringAndSize(obj_, &exc_str_data, &exc_str_len);
    maybe_throw(result < 0);
    return string(exc_str_data, exc_str_len);
}

CCPyObj CCPyObj::attr(CCPyObj name) const {
    return checked_steal(PyObject_GetAttr(obj_, name.obj_));
}

ssize_t CCPyObj::size() const {
    Py_ssize_t result = PyObject_Size(obj_);
    maybe_throw(result == -1 && PyErr_Occurred());
    return result;
}

CCPyObj CCPyObj::item(int idx) const {
    return checked_steal(PySequence_GetItem(obj_, idx));
}

CCPyObj CCPyObj::item(const string &key) const {
    return checked_steal(
        PyMapping_GetItemString(obj_, const_cast<char *>(key.c_str())));
}

// static
void CCPyObj::maybe_throw(bool cond) {
    static int maybe_throw_depth = 0;
    Counter counter{maybe_throw_depth};

    if (!cond) {
        return;
    }

    assert(PyErr_Occurred());

    // If we couldn't import the traceback module or we're already
    // formatting an exception, just print the exception to stderr.
    if (!traceback_mod.ref() || maybe_throw_depth > 1) {
        PyErr_Print();
        PyErr_Clear();
        throw PyError{"Unhandled exception"};
    }

    // Get (and clear) the current exception information.
    CCPyObj exc_type, exc_obj, exc_tb;
    {
        PyObject *raw_type, *raw_obj, *raw_tb;
        PyErr_Fetch(&raw_type, &raw_obj, &raw_tb);
        PyErr_NormalizeException(&raw_type, &raw_obj, &raw_tb);
        exc_type = unchecked_steal(raw_type);
        exc_obj = unchecked_steal(raw_obj);
        exc_tb = unchecked_steal(raw_tb);
    }
    if (!exc_tb.obj_) {
        exc_tb = unchecked_borrow(Py_None);
    }

    // Format the exception.
    CCPyObj result;
    try {
        CCPyObj lines =
            traceback_mod.attr("format_exception")(exc_type, exc_obj, exc_tb);
        result = CCPyObj{""}.attr("join")(lines);
    } catch (PyError &exc) {
        // If we can't format the exception, print both exceptions.
        PyErr_Restore(exc_type.new_ref(), exc_obj.new_ref(), exc_tb.new_ref());
        PyErr_Print();
        PyErr_Clear();
        throw PyError{string{"Unable to format exception "
                             "(traceback.format_exception):\n"} +
                      exc.what()};
    }

    throw PyError{result};
}

CCPyObj CCPyObj::call(vector<CCPyObj> args) {
    CCPyObj args2 = checked_steal(PyTuple_New(args.size()));
    for (unsigned int i = 0; i < args.size(); i++) {
        PyTuple_SET_ITEM(args2.obj_, i, args[i].new_ref());
    }
    return checked_steal(PyObject_Call(obj_, args2.obj_, nullptr));
}

}  // namespace slapo_py_update_hook
