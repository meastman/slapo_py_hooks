#include "portable.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include <ac/string.h>
#include <ac/socket.h>

#include "lutil.h"
#include "slap.h"
#include "config.h"

#define SIDE_LDAP
#include "slapo_py_update_hook.h"

using std::map;
using std::string;
using std::vector;
using slapo_py_update_hook::InstanceInfo;
using slapo_py_update_hook::ModificationOp;

namespace {

string Bv2String(const BerValue& src) {
  if (src.bv_len > 0)
    return string(src.bv_val, src.bv_len);
  return "";
}

void String2Bv(const string& src, BerValue* dst) {
  dst->bv_len = src.size();
  if (src.empty()) {
    dst->bv_val = NULL;
  } else {
    dst->bv_val = (char*) malloc(src.size());
    assert(dst->bv_val != NULL);
    memcpy(dst->bv_val, src.data(), src.size());
  }
}

int InitHook(BackendDB* be, ConfigReply* cr) {
  slap_overinst* on = (slap_overinst*) be->bd_info;
  on->on_bi.bi_private = (void*) new InstanceInfo;
  return LDAP_SUCCESS;
}

int ConfigHook(
    BackendDB* be, const char* fname, int lineno, int argc, char** argv) {
  slap_overinst* on = (slap_overinst*) be->bd_info;
  InstanceInfo* info = (InstanceInfo*) on->on_bi.bi_private;
  assert(info != NULL);

  if (strcmp(argv[0], "py_filename") == 0) {
    if (argc == 2) {
      info->SetFilename(string(argv[1]));
      return LDAP_SUCCESS;
    } else {
      Log2(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR,
           "Wrong number of args for py_filename in %s on line %d\n",
           fname, lineno);
      return LDAP_PARAM_ERROR;
    }
  } else if (strcmp(argv[0], "py_function") == 0) {
    if (argc == 2) {
      info->SetFunctionName(string(argv[1]));
      return LDAP_SUCCESS;
    } else {
      Log2(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR,
           "Wrong number of args for py_function in %s on line %d\n",
           fname, lineno);
      return LDAP_PARAM_ERROR;
    }
  } else {
    return SLAP_CONF_UNKNOWN;
  }
}

int OpenHook(BackendDB* be, ConfigReply* cr) {
  slap_overinst* on = (slap_overinst*) be->bd_info;
  InstanceInfo* info = (InstanceInfo*) on->on_bi.bi_private;
  assert(info != NULL);
  return info->Open() ? LDAP_SUCCESS : LDAP_PARAM_ERROR;
}

int ModifyHook(Operation *op, SlapReply *rs) {
  slap_overinst* on = (slap_overinst*) op->o_bd->bd_info;
  InstanceInfo* info = (InstanceInfo*) on->on_bi.bi_private;
  assert(info != NULL);

  ModificationOp m2;
  m2.FromLdap(op->orm_modlist);
  slap_mods_free(op->orm_modlist, 1);
  op->orm_modlist = NULL;
  m2.dn = Bv2String(op->o_req_dn);

  string error;
  int status = info->Update(&m2, &error);
  if (status != LDAP_SUCCESS) {
    op->o_bd->bd_info = (BackendInfo*) on->on_info;
    send_ldap_error(op, rs, status, error.c_str());
    return status;
  }

  status = m2.ToLdap(&op->orm_modlist, &error);
  if (status != LDAP_SUCCESS) {
    slap_mods_free(op->orm_modlist, 1);
    op->orm_modlist = NULL;
    op->o_bd->bd_info = (BackendInfo*) on->on_info;
    send_ldap_error(op, rs, status, error.c_str());
    return status;
  }

  return SLAP_CB_CONTINUE;
}

int DestroyHook(BackendDB* be, ConfigReply* cr) {
  slap_overinst* on = (slap_overinst*) be->bd_info;
  InstanceInfo* info = (InstanceInfo*) on->on_bi.bi_private;
  delete info;
  on->on_bi.bi_private = NULL;
  return LDAP_SUCCESS;
}

}  // anonymous namespace

namespace slapo_py_update_hook {

void ModificationOp::FromLdap(const Modifications* mod_list) {
  for (const Modifications* in_mod = mod_list;
       in_mod != NULL; in_mod = in_mod->sml_next) {
    Modification out_mod;

    assert(in_mod->sml_desc != NULL);
    out_mod.name = Bv2String(in_mod->sml_desc->ad_cname);
    for (size_t i = 0; i < in_mod->sml_numvals; i++) {
      out_mod.values.push_back(Bv2String(in_mod->sml_values[i]));
    }
    out_mod.op = in_mod->sml_op;
    out_mod.flags = in_mod->sml_flags;
    mods.push_back(out_mod);
  }
}

int ModificationOp::ToLdap(Modifications** mod_list, string* error) {
  for (vector<Modification>::const_iterator in_mod = mods.begin();
       in_mod != mods.end(); ++in_mod) {
    Modifications* out_mod = (Modifications*) calloc(1, sizeof(Modifications));
    *mod_list = out_mod;
    mod_list = &out_mod->sml_next;

    BerValue name;
    name.bv_len = in_mod->name.size();
    name.bv_val = const_cast<char*>(in_mod->name.data());
    AttributeDescription* ad = NULL;
    const char* text;
    int status = slap_bv2ad(&name, &ad, &text);
    if (status != LDAP_SUCCESS) {
      *error = "Invalid attribute: " + in_mod->name;
      return status;
    }
    out_mod->sml_desc = ad;

    out_mod->sml_op = in_mod->op;
    out_mod->sml_flags = in_mod->flags;
    out_mod->sml_numvals = in_mod->values.size();
    out_mod->sml_values = (BerValue*) calloc(
        in_mod->values.size() + 1, sizeof(BerValue));
    for (size_t i = 0; i < in_mod->values.size(); i++) {
      String2Bv(in_mod->values[i], &out_mod->sml_values[i]);
    }
    BER_BVZERO(&out_mod->sml_values[in_mod->values.size()]);
    out_mod->sml_nvalues = NULL;
  }

  return LDAP_SUCCESS;
}

void LogFromPython(const string& message) {
  Log1(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR, "%s\n", message.c_str());
}

}  // namespace slapo_py_update_hook

extern "C"
int init_module(int argc, char *argv[]) {
  map<string, int> py_consts;
  py_consts["SLAP_MOD_INTERNAL"] = SLAP_MOD_INTERNAL;
  py_consts["SLAP_MOD_MANAGING"] = SLAP_MOD_MANAGING;
  py_consts["LDAP_MOD_ADD"] = LDAP_MOD_ADD;
  py_consts["LDAP_MOD_DELETE"] = LDAP_MOD_DELETE;
  py_consts["LDAP_MOD_REPLACE"] = LDAP_MOD_REPLACE;

  if (!slapo_py_update_hook::GlobalInit(py_consts)) {
    return LDAP_OTHER;
  }

  static slap_overinst overlay;
  memset(&overlay, 0, sizeof(overlay));
  overlay.on_bi.bi_type = const_cast<char*>("py_update_hook");
  overlay.on_bi.bi_db_init = &InitHook;
  overlay.on_bi.bi_db_config = &ConfigHook;
  overlay.on_bi.bi_db_open = &OpenHook;
  overlay.on_bi.bi_op_modify = &ModifyHook;
  overlay.on_bi.bi_db_destroy = &DestroyHook;
  return overlay_register(&overlay);
}
