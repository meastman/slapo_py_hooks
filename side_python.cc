#include <Python.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#define SIDE_PYTHON
#include "slapo_py_update_hook.h"

using std::map;
using std::string;
using std::vector;
using slapo_py_update_hook::LogFromPython;
using slapo_py_update_hook::Modification;
using slapo_py_update_hook::ModificationOp;

namespace {
PyObject* traceback_mod = NULL;
PyObject* op_type = NULL;
PyObject* mod_type = NULL;
map<string, int>* py_consts = NULL;

void LogPyExc();

PyObject* InlineInc(PyObject* obj) {
  Py_XINCREF(obj);
  return obj;
}

template <typename IntType>
bool PyIntOrLongAsInt(PyObject* py_int, IntType* cc_int) {
  if (PyLong_CheckExact(py_int)) {
    *cc_int = PyLong_AsLong(py_int);
  } else {
    *cc_int = PyInt_AsLong(py_int);
  }
  return PyErr_Occurred() == NULL;
}

class PyObjectOwner {
 public:
  PyObjectOwner() { }
  ~PyObjectOwner() {
    for (vector<PyObject*>::const_iterator i = objs_.begin();
         i != objs_.end(); ++i) {
      Py_XDECREF(*i);
    }
  }

  PyObject* Own(PyObject* obj) {
    objs_.push_back(obj);
    return obj;
  }

 private:
  vector<PyObject*> objs_;
  DISALLOW_COPY_AND_ASSIGN(PyObjectOwner);
};

PyObject* PyStrFromStr(const string& str_in) {
  return PyString_FromStringAndSize(str_in.data(), str_in.size());
}

string StrFromPyStr(PyObject* str_in) {
  char* exc_str_data = NULL;
  Py_ssize_t exc_str_len = 0;
  PyString_AsStringAndSize(str_in, &exc_str_data, &exc_str_len);
  return string(exc_str_data, exc_str_len);
}

void LogPyExc() {
  assert(PyErr_Occurred());
  if (traceback_mod == NULL) {
    PyErr_Print();
    PyErr_Clear();
    return;
  }

  PyObject* exc_type = NULL;
  PyObject* exc_obj = NULL;
  PyObject* exc_tb = NULL;
  PyErr_Fetch(&exc_type, &exc_obj, &exc_tb);
  PyErr_NormalizeException(&exc_type, &exc_obj, &exc_tb);
  if (exc_tb == NULL) {
    exc_tb = InlineInc(Py_None);
  }

  PyObjectOwner owner;
  PyObject* result = owner.Own(PyObject_CallMethodObjArgs(
      traceback_mod, owner.Own(PyStrFromStr("format_exception")),
      exc_type, exc_obj, exc_tb, NULL));
  if (result == NULL) {
    LogFromPython("Unable to format exception (traceback.format_exception)");
    PyErr_Print();
    PyErr_Restore(exc_type, exc_obj, exc_tb);
    PyErr_Print();
    PyErr_Clear();
    return;
  }

  // py_result_str = ''.join(result)
  PyObject* py_result_str = owner.Own(PyObject_CallMethodObjArgs(
      owner.Own(PyStrFromStr("")), owner.Own(PyStrFromStr("join")),
      result, NULL));
  if (py_result_str == NULL || !PyString_CheckExact(py_result_str)) {
    LogFromPython("Unable to format exception (str.join)");
    PyErr_Print();
    PyErr_Restore(exc_type, exc_obj, exc_tb);
    PyErr_Print();
    PyErr_Clear();
    return;
  }

  Py_DECREF(exc_type);
  Py_DECREF(exc_obj);
  Py_DECREF(exc_tb);

  LogFromPython(StrFromPyStr(py_result_str));
}

class GilHolder {
 public:
  GilHolder() : state_(PyGILState_Ensure()) { }
  ~GilHolder() { PyGILState_Release(state_); }

 private:
  PyGILState_STATE state_;
  DISALLOW_COPY_AND_ASSIGN(GilHolder);
};

class GilReleaser {
 public:
  GilReleaser() { }
  ~GilReleaser() { PyEval_SaveThread(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(GilReleaser);
};

}  // anonymous namespace

namespace slapo_py_update_hook {

bool ModificationOp::FromPython(PyObject* py_op) {
  mods_.clear();
  PyObjectOwner owner;
  PyObject* mods = owner.Own(PyObject_GetAttrString(py_op, "modifications"));
  if (mods == NULL) {
    LogPyExc();
    return false;
  }

  Py_ssize_t num_mods = PyList_Size(mods);
  for (Py_ssize_t i = 0; i < num_mods; i++) {
    PyObject* py_mod = PyList_GetItem(mods, i);
    if (PyObject_Size(py_mod) != 4) {
      LogFromPython(
          "Invalid modification (mods should only contain len-4-tuples or "
          "Modification namedtuples)");
      return false;
    }

    Modification mod;

    PyObject* py_name = owner.Own(PySequence_GetItem(py_mod, 0));
    if (!PyString_CheckExact(py_name)) {
      LogFromPython("Invalid modification (element 0 should be a string)");
      return false;
    }
    mod.name = StrFromPyStr(py_name);

    PyObject* iterator = owner.Own(PyObject_GetIter(
        owner.Own(PySequence_GetItem(py_mod, 1))));
    if (iterator == NULL) {
      LogPyExc();
      return false;
    }

    PyObject* item;
    while ((item = owner.Own(PyIter_Next(iterator))) != NULL) {
      if (!PyString_CheckExact(item)) {
        LogFromPython("Values must all be strings");
        return false;
      }
      mod.values.push_back(StrFromPyStr(item));
    }
    if (PyErr_Occurred()) {
      LogPyExc();
      return false;
    }

    if (!PyIntOrLongAsInt(owner.Own(
            PySequence_GetItem(py_mod, 2)), &mod.op) ||
        !PyIntOrLongAsInt(owner.Own(
            PySequence_GetItem(py_mod, 3)), &mod.flags)) {
      LogPyExc();
      return false;
    }

    mods_.push_back(mod);
  }

  return true;
}

PyObject* ModificationOp::ToPython() {
  PyObjectOwner owner;

  PyObject* entry = owner.Own(PyDict_New());
  for (map<string, vector<string> >::const_iterator attr = entry_.begin();
       attr != entry_.end(); ++attr) {
    PyObject* values = owner.Own(PyList_New(0));
    for (vector<string>::const_iterator value = attr->second.begin();
         value != attr->second.end(); ++value) {
      PyList_Append(values, owner.Own(PyStrFromStr(*value)));
    }
    PyDict_SetItem(entry, owner.Own(PyStrFromStr(attr->first)), values);
  }

  PyObject* mods = owner.Own(PyList_New(0));
  for (vector<Modification>::const_iterator mod = mods_.begin();
       mod != mods_.end(); ++mod) {
    PyObject* py_values = owner.Own(PyList_New(0));
    for (vector<string>::const_iterator value = mod->values.begin();
         value != mod->values.end(); ++value) {
      PyList_Append(py_values, owner.Own(PyStrFromStr(*value)));
    }

    PyObject* py_mod = owner.Own(PyObject_CallFunctionObjArgs(
        mod_type, owner.Own(PyStrFromStr(mod->name)), py_values,
        owner.Own(PyInt_FromLong(mod->op)),
        owner.Own(PyInt_FromLong(mod->flags)), NULL));
    if (py_mod == NULL) {
      return py_mod;
    }
    PyList_Append(mods, py_mod);
  }

  return PyObject_CallFunctionObjArgs(
      op_type, owner.Own(PyStrFromStr(dn_)), owner.Own(PyStrFromStr(auth_dn_)),
      entry, mods, NULL);
}

bool GlobalInit(const map<string, int>& new_py_consts) {
  py_consts = new map<string, int>(new_py_consts);
  Py_InitializeEx(0);
  PyEval_InitThreads();
  GilReleaser gil_releaser;

  traceback_mod = PyImport_ImportModule("traceback");
  if (traceback_mod == NULL) {
    LogPyExc();
    return false;
  }

  // This is nasty. Ideally we'd use PyImport_ImportModule and
  // PyObject_CallMethod or PyObject_CallMethodObjArgs, but the namedtuple
  // function looks at the caller's stack frame, which wouldn't exist and
  // would raise an exception.
  PyObjectOwner owner;
  PyObject* globals = owner.Own(PyDict_New());
  PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
  PyObject* locals = owner.Own(PyDict_New());
  if (owner.Own(PyRun_String(
      "import collections\n"
      "op_type = collections.namedtuple(\n"
      "'ModificationOp', 'dn,auth_dn,entry,modifications')\n"
      "mod_type = collections.namedtuple(\n"
      "'Modification', 'name,values,op,flags')\n",
      Py_file_input, globals, locals)) == NULL) {
    LogPyExc();
    return false;
  }
  op_type = InlineInc(PyDict_GetItemString(locals, "op_type"));
  if (op_type == NULL) {
    LogPyExc();
    return false;
  }
  mod_type = InlineInc(PyDict_GetItemString(locals, "mod_type"));
  if (mod_type == NULL) {
    LogPyExc();
    return false;
  }

  return true;
}

InstanceInfo::~InstanceInfo() {
  Py_XDECREF(py_module_);
}

bool InstanceInfo::Open() {
  if (filename_.empty()) {
    LogFromPython("No py_filename specified in config");
    return false;
  }
  FILE* fp = fopen(filename_.c_str(), "r");
  if (fp == NULL) {
    LogFromPython("Unable to open file " + filename_ + ": " + strerror(errno));
    return false;
  }

  GilHolder gil_holder;
  PyObjectOwner owner;
  PyObject* mod = owner.Own(PyModule_New("update_hook"));
  PyModule_AddStringConstant(mod, "__file__", filename_.c_str());
  for (map<string, int>::const_iterator i = py_consts->begin();
       i != py_consts->end(); ++i) {
    PyModule_AddIntConstant(mod, i->first.c_str(), i->second);
  }
  PyModule_AddObject(
      mod, "__builtins__", InlineInc(PyEval_GetBuiltins()));
  PyModule_AddObject(
      mod, "Modification", InlineInc(mod_type));
  PyObject* locals = owner.Own(PyDict_New());
  PyObject* result = owner.Own(PyRun_FileEx(
      fp, filename_.c_str(), Py_file_input, PyModule_GetDict(mod), locals, 1));
  if (result == NULL) {
    LogPyExc();
    return false;
  }
  PyDict_Update(PyModule_GetDict(mod), locals);

  if (!PyObject_HasAttrString(mod, function_name_.c_str())) {
    LogFromPython("File " + filename_ + " is missing function " +
                  function_name_);
    return false;
  }

  py_module_ = InlineInc(mod);
  return true;
}

int InstanceInfo::Update(
    ModificationOp* op, string* error) {
  assert(py_module_ != NULL);
  GilHolder gil_holder;

  PyObjectOwner owner;
  PyObject* py_op = owner.Own(op->ToPython());
  if (py_op == NULL) {
    LogPyExc();
    return 0x50;  // LDAP_OTHER
  }

  PyObject* result = owner.Own(PyObject_CallMethodObjArgs(
      py_module_, owner.Own(PyStrFromStr(function_name_)), py_op, NULL));
  if (result == NULL) {
    LogPyExc();
    return 0x50;  // LDAP_OTHER
  }

  if (result != Py_None) {
    int status;
    if (!PyTuple_CheckExact(result) || PyTuple_Size(result) != 2 ||
        !PyIntOrLongAsInt(PyTuple_GetItem(result, 0), &status) ||
        !PyString_CheckExact(PyTuple_GetItem(result, 1))) {
      LogFromPython("Result must be None or (int, str)");
      return false;
    }

    if (status != 0) {
      *error = StrFromPyStr(PyTuple_GetItem(result, 1));
      return status;
    }
  }

  if (!op->FromPython(py_op)) {
    return 0x50;  // LDAP_OTHER
  }

  return 0;  // LDAP_SUCCESS
}

}  // namespace slapo_py_update_hook
