#ifndef CC_PY_OBJ_H_
#define CC_PY_OBJ_H_

#include <Python.h>

#include <string>
#include <vector>

namespace slapo_py_update_hook {

void init_cc_py_obj();

class CCPyObj {
  public:
    CCPyObj();
    CCPyObj(long);
    CCPyObj(const char *);
    CCPyObj(const std::string &);
    CCPyObj(const CCPyObj &);
    ~CCPyObj();

    CCPyObj &operator=(const CCPyObj &);
    static CCPyObj checked_borrow(PyObject *);
    static CCPyObj unchecked_borrow(PyObject *);
    static CCPyObj checked_steal(PyObject *);
    static CCPyObj unchecked_steal(PyObject *);

    PyObject *ref();
    PyObject *new_ref();

    operator long() const;
    operator std::string() const;

    CCPyObj attr(CCPyObj name) const;
    CCPyObj native_str() const;
    ssize_t size() const;
    CCPyObj item(int) const;
    CCPyObj item(const std::string &) const;

    template <class... Args>
    CCPyObj operator()(Args... args) {
        return call({}, args...);
    }

  private:
    static void maybe_throw(bool cond);

    CCPyObj call(std::vector<CCPyObj> args);

    template <class First, class... Args>
    CCPyObj call(std::vector<CCPyObj> args, First more1, Args... more2) {
        args.emplace_back(more1);
        return call(args, more2...);
    }

    PyObject *obj_;
};

}  // namespace slapo_py_update_hook

#endif  // CC_PY_OBJ_H_
