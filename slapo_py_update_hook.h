#ifndef SLAPO_PY_UPDATE_HOOK_H_
#define SLAPO_PY_UPDATE_HOOK_H_

#include <map>
#include <string>
#include <vector>

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)
#endif  // DISALLOW_COPY_AND_ASSIGN

#ifdef SIDE_LDAP
struct PyObject;
#endif
#ifdef SIDE_PYTHON
struct Modifications;
#endif

namespace slapo_py_update_hook {

struct Modification {
  std::string name;
  std::vector<std::string> values;
  int op;
  int flags;
};

struct ModificationOp {
  std::string dn;
  std::vector<Modification> mods;

  void FromLdap(const Modifications* mod_list);
  int ToLdap(Modifications** mod_list, std::string* error);
  bool FromPython(PyObject* py_mods);
  bool ToPython(PyObject* py_mods);
};

void LogFromPython(const std::string& message);
bool GlobalInit(const std::map<std::string, int>& py_consts);

class InstanceInfo {
 public:
  InstanceInfo()
      : function_name_("Update"), py_module_(NULL) { }
  ~InstanceInfo();

  void SetFilename(const std::string& name) {
    filename_ = name;
  }
  void SetFunctionName(const std::string& name) {
    function_name_ = name;
  }
  bool Open();
  int Update(
      ModificationOp* op,
      std::string* error);

 private:
  std::string filename_;
  std::string function_name_;
  PyObject* py_module_;
  DISALLOW_COPY_AND_ASSIGN(InstanceInfo);
};

}  // namespace slapo_py_update_hook

#endif  // SLAPO_PY_UPDATE_HOOK_H_
