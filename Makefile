PYTHON_BIN = python
PY_VERSION = $(shell $(PYTHON_BIN) -c 'import sys; print("{0}.{1}".format(*sys.version_info))')
CXXFLAGS = -Wall -Werror -fPIC -std=c++11

default: all
all: py_update_hook.so

clean:
	rm -f *.o *.so

side_ldap.o: side_ldap.cc slapo_py_update_hook.h
	$(CXX) $(CXXFLAGS) -I $(OPENLDAP_DIR)/include -I $(OPENLDAP_DIR)/servers/slapd -o $@ -c $<
side_python.o: side_python.cc slapo_py_update_hook.h cc_py_obj.h
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags python-$(PY_VERSION)) -o $@ -c $<
cc_py_obj.o: cc_py_obj.cc slapo_py_update_hook.h cc_py_obj.h
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags python-$(PY_VERSION)) -o $@ -c $<
py_update_hook.so: side_ldap.o side_python.o cc_py_obj.o
	$(CXX) -shared -o $@ $^ $(shell pkg-config --libs python-$(PY_VERSION)) -lstdc++
