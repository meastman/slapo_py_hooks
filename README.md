# slapo\_py\_hooks

slapo\_py\_hooks is an openldap overlay which allows you to write update hooks
in Python.

## Building

- You must have downloaded the openldap source, extracted it, and at least run
  configure.
- Run `make OPENLDAP_DIR=/path/to/openldap/dir`

## Installing

- Copy `py_update_hook.so` somewhere where slapd will be able to read it.

## Configuring (slapd.conf)

- Near the other moduleload lines, add `moduleload /path/to/py_update_hook.so`
- In the section for the database, add the following:
  - `overlay py_update_hook` - use this overlay for this database
  - `py_filename /path/to/python/script.py` - specify the path to the file
    containing the update hook. If `overlay py_update_hook` is specified, this
    directive is required.
  - `py_function SomeFunctionName` - specify an alternate function name for
    the hook. The default is `Update`.

## Hooks

- Your hook function/file will have access to additional globals:
  - `Modification`: a namedtuple type described below
  - Various openldap constants, including: `LDAP_MOD_ADD`, `LDAP_MOD_DELETE`,
    `LDAP_MOD_REPLACE`, `SLAP_MOD_INTERNAL`, and `SLAP_MOD_MANAGING`
- Your hook function is called *before* any ACL checks. Be careful!
- Your function should be named `Update` unless you override `py_function` in
  slapd.conf
- Your function should take a single argument, an object with the following
  attributes:
    - `dn`: a string containing the DN of the entry being modified.
    - `auth_dn`: a string containing the DN of the authenticated user.
    - `entry`: a dict `{attribute_name: [value, ...]}` containing the current
       attributes of the entry.
    - `modifications`: a list of `Modification` namedtuples, each of which contains
       the following:
        - `name`: a string containing the attribute name
        - `values`: a list of strings containing values to add/remove
        - `op`: an int indicating the type of modification; one of:
          `LDAP_MOD_ADD`, `LDAP_MOD_DELETE`, or `LDAP_MOD_REPLACE`
        - `flags`: an int containing a bitmask of flags, such as `SLAP_MOD_INTERNAL`
          which means that ACL checks should not be performed for this attribute
- You may add or remove entries from the modifications list. Any added
  modification may either be a `Modification` namedtuple or a normal tuple
  containing `(name, values, op, flags)`.
- You can either return `None` which indicates that processing the request
  should continue (with the possibly modified list of modifications) or a
  tuple of `(int_status, str_error_message)` which causes that error to be
  returned to the client. If your code raises an exception, a status of
  `LDAP_OTHER` is returned to the client and the exception information is logged
  (but not returned to the client).
