#include <Python.h>

#include <cassert>
#include <memory>
#include <string>

#include "slapo_py_update_hook.h"
#include "cc_py_obj.h"

using std::string;
using std::unique_ptr;

namespace slapo_py_update_hook {
namespace {

CCPyObj op_type;
CCPyObj mod_type;

class GilHolder {
  public:
    GilHolder() : state_(PyGILState_Ensure()) {}
    GilHolder(const GilHolder &) = delete;
    ~GilHolder() { PyGILState_Release(state_); }
    void operator=(const GilHolder &) = delete;

  private:
    PyGILState_STATE state_;
};

}  // anonymous namespace

//
// ModificationOp
//

CCPyObj mod_op_to_python(ModificationOp &op) {
    CCPyObj py_entry = CCPyObj::checked_steal(PyDict_New());
    for (const auto &name_values : op.entry) {
        CCPyObj values = CCPyObj::checked_steal(PyList_New(0));
        for (const string &value : name_values.second) {
            CCPyObj py_value{value};
            PyList_Append(values.ref(), py_value.ref());
        }
        CCPyObj py_name{name_values.first};
        PyDict_SetItem(py_entry.ref(), py_name.ref(), values.ref());
    }

    CCPyObj py_mods = CCPyObj::checked_steal(PyList_New(0));
    for (const Modification &mod : op.mods) {
        CCPyObj py_values = CCPyObj::checked_steal(PyList_New(0));
        for (const string &value : mod.values) {
            CCPyObj py_value{value};
            PyList_Append(py_values.ref(), py_value.ref());
        }

        CCPyObj py_mod = mod_type(mod.name, py_values, mod.op, mod.flags);
        PyList_Append(py_mods.ref(), py_mod.ref());
    }

    return op_type(op.dn, op.auth_dn, py_entry, py_mods);
}

void mod_op_from_python(ModificationOp &op, CCPyObj py_op) {
    op.mods.clear();

    CCPyObj mods = py_op.attr("modifications");
    Py_ssize_t num_mods = mods.size();
    for (Py_ssize_t i = 0; i < num_mods; i++) {
        CCPyObj py_mod = mods.item(i);
        if (py_mod.size() != 4) {
            throw PyError{"Invalid modification (mods should only contain "
                          "len-4-tuples or "
                          "Modification namedtuples)"};
        }

        Modification mod;

        mod.name = static_cast<string>(py_mod.item(0));

        CCPyObj values = py_mod.item(1);
        CCPyObj iterator =
            CCPyObj::checked_steal(PyObject_GetIter(values.ref()));
        CCPyObj item;
        while ((item = CCPyObj::unchecked_steal(PyIter_Next(iterator.ref())))
                   .ref() != nullptr) {
            mod.values.push_back(item);
        }

        mod.op = py_mod.item(2);
        mod.flags = py_mod.item(3);
        op.mods.push_back(mod);
    }
}

//
// Misc
//

void init_python() {
    Py_InitializeEx(0);
    PyEval_InitThreads();
    PyEval_SaveThread();
    GilHolder gil_holder;

    init_cc_py_obj();

    // Ideally we'd use PyImport_ImportModule and PyObject_CallMethod or
    // PyObject_CallMethodObjArgs, but the namedtuple function looks at the
    // caller's stack frame, which wouldn't exist and would raise an exception.
    CCPyObj globals = CCPyObj::checked_steal(PyDict_New());
    PyDict_SetItemString(globals.ref(), "__builtins__", PyEval_GetBuiltins());
    CCPyObj locals = CCPyObj::checked_steal(PyDict_New());

    CCPyObj::checked_steal(
        PyRun_String("import collections\n"
                     "op_type = collections.namedtuple(\n"
                     "'ModificationOp', 'dn,auth_dn,entry,modifications')\n"
                     "mod_type = collections.namedtuple(\n"
                     "'Modification', 'name,values,op,flags')\n",
                     Py_file_input, globals.ref(), locals.ref()));
    op_type = locals.item("op_type");
    mod_type = locals.item("mod_type");
}

//
// InstanceInfo
//

class InstanceInfoImpl : public InstanceInfo {
  public:
    InstanceInfoImpl() : function_name_("update") {}
    virtual ~InstanceInfoImpl() {}

    void set_filename(const std::string &name) override { filename_ = name; }
    void set_function_name(const std::string &name) override {
        function_name_ = name;
    }
    void open() override;
    int update(ModificationOp &op, std::string &error) override;

  private:
    std::string filename_;
    std::string function_name_;
    CCPyObj py_module_;
};

void InstanceInfoImpl::open() {
    if (filename_.empty()) {
        throw PyError{"No py_filename specified in config"};
    }

    unique_ptr<FILE, decltype(&fclose)> fp{nullptr, &fclose};
    fp.reset(fopen(filename_.c_str(), "r"));
    if (!fp) {
        throw PyError{"Unable to open file " + filename_ + ": " +
                      strerror(errno)};
    }

    GilHolder gil_holder;
    CCPyObj mod = CCPyObj::checked_steal(PyModule_New("update_hook"));
    PyModule_AddStringConstant(mod.ref(), "__file__", filename_.c_str());
    for (const auto &name_value : py_consts) {
        PyModule_AddIntConstant(mod.ref(), name_value.first.c_str(),
                                name_value.second);
    }
    CCPyObj builtins = CCPyObj::checked_borrow(PyEval_GetBuiltins());
    PyModule_AddObject(mod.ref(), "__builtins__", builtins.new_ref());
    PyModule_AddObject(mod.ref(), "Modification", mod_type.new_ref());
    CCPyObj locals = CCPyObj::checked_steal(PyDict_New());
    CCPyObj::checked_steal(
        PyRun_FileEx(fp.get(), filename_.c_str(), Py_file_input,
                     PyModule_GetDict(mod.ref()), locals.ref(), 0));
    PyDict_Update(PyModule_GetDict(mod.ref()), locals.ref());

    if (!PyObject_HasAttrString(mod.ref(), function_name_.c_str())) {
        throw PyError{"File " + filename_ + " is missing function " +
                      function_name_};
    }

    py_module_ = mod;
}

int InstanceInfoImpl::update(ModificationOp &op, string &error) {
    assert(py_module_.ref());
    GilHolder gil_holder;

    CCPyObj py_op = mod_op_to_python(op);
    CCPyObj result = py_module_.attr(function_name_)(py_op);
    if (result.ref() != Py_None) {
        if (result.size() != 2) {
            throw PyError{"Result must be None or (int, str)"};
        }
        int status = result.item(0);
        if (status != 0) {
            error = static_cast<string>(result.item(1));
            return status;
        }
    }

    mod_op_from_python(op, py_op);
    return 0;  // LDAP_SUCCESS
}

// static
InstanceInfo *InstanceInfo::create() { return new InstanceInfoImpl; }

}  // namespace slapo_py_update_hook
