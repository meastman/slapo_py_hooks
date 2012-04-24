PY_VERSION = $(shell python -c 'import sys; print "%d.%d" % sys.version_info[:2]')
PY_INC_DIR = /usr/include/python$(PY_VERSION)
PY_LIB_NAME = python$(PY_VERSION)
OPENLDAP_DIR = $(HOME)  # probably needs to be overridden!
CXX = g++
CXXFLAGS = -Wall -Werror -fPIC

default: all
all: py_update_hook.so

clean:
	rm -f *.o *.so

side_ldap.o: side_ldap.cc slapo_py_update_hook.h
	$(CXX) $(CXXFLAGS) -I $(OPENLDAP_DIR)/include -I $(OPENLDAP_DIR)/servers/slapd -o $@ -c $<
side_python.o: side_python.cc slapo_py_update_hook.h
	$(CXX) $(CXXFLAGS) -I $(PY_INC_DIR) -o $@ -c $<
py_update_hook.so: side_ldap.o side_python.o
	$(CXX) -shared -l$(PY_LIB_NAME) -lstdc++ -o $@ $^
