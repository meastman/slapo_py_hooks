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
struct BerValue;
struct Entry;
struct Modifications;
#endif

namespace slapo_py_update_hook {

struct Modification {
  std::string name;
  std::vector<std::string> values;
  int op;
  int flags;
};

class ModificationOp {
 public:
  void FromLdap(
      const BerValue& dn, const BerValue& auth_dn, const Modifications* mods);
  void AddEntry(const Entry* entry);
  int ToLdap(Modifications** mods, std::string* error);
  bool FromPython(PyObject* op);
  PyObject* ToPython();

 private:
  std::string dn_;
  std::string auth_dn_;
  std::map<std::string, std::vector<std::string> > entry_;
  std::vector<Modification> mods_;
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
