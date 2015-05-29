#ifndef SLAPO_PY_UPDATE_HOOK_H_
#define SLAPO_PY_UPDATE_HOOK_H_

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace slapo_py_update_hook {

extern const std::map<std::string, int> py_consts;

class PyError : public std::runtime_error {
  public:
    PyError(const std::string &arg) : std::runtime_error{arg} {}
};

struct Modification {
    std::string name;
    std::vector<std::string> values;
    int op;
    int flags;
};

struct ModificationOp {
    std::string dn;
    std::string auth_dn;
    std::map<std::string, std::vector<std::string>> entry;
    std::vector<Modification> mods;
};

void init_python();

class InstanceInfo {
  public:
    static InstanceInfo *create();
    virtual ~InstanceInfo() {}
    virtual void set_filename(const std::string &) = 0;
    virtual void set_function_name(const std::string &) = 0;
    virtual void open() = 0;
    virtual int update(ModificationOp &op, std::string &error) = 0;

  protected:
    InstanceInfo() {}
};

}  // namespace slapo_py_update_hook

#endif  // SLAPO_PY_UPDATE_HOOK_H_
