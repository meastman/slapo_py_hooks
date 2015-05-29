#include "portable.h"

#include <cassert>
#include <cstring>  // memcpy
#include <map>
#include <string>
#include <vector>

#include "lutil.h"
#include "slap.h"
#include "config.h"

#include "slapo_py_update_hook.h"

using std::map;
using std::string;
using std::vector;

namespace slapo_py_update_hook {

const map<string, int> py_consts{
    {"SLAP_MOD_INTERNAL", SLAP_MOD_INTERNAL},
    {"SLAP_MOD_MANAGING", SLAP_MOD_MANAGING},
    {"LDAP_MOD_ADD", LDAP_MOD_ADD},
    {"LDAP_MOD_DELETE", LDAP_MOD_DELETE},
    {"LDAP_MOD_REPLACE", LDAP_MOD_REPLACE},
};

namespace {

//
// Utils
//

string bv_to_string(const BerValue &src) {
    if (src.bv_len > 0)
        return string(src.bv_val, src.bv_len);
    return "";
}

void string_to_bv(const string &src, BerValue *dst) {
    dst->bv_len = src.size();
    if (src.empty()) {
        dst->bv_val = nullptr;
    } else {
        dst->bv_val = static_cast<char *>(ch_malloc(src.size()));
        memcpy(dst->bv_val, src.data(), src.size());
    }
}

void mod_op_from_ldap(ModificationOp &op, const BerValue &dn,
                      const BerValue &auth_dn, const Modifications *mods) {
    op.dn = bv_to_string(dn);
    op.auth_dn = bv_to_string(auth_dn);

    for (const Modifications *in_mod = mods; in_mod;
         in_mod = in_mod->sml_next) {
        Modification out_mod;

        assert(in_mod->sml_desc);
        out_mod.name = bv_to_string(in_mod->sml_desc->ad_cname);
        for (size_t i = 0; i < in_mod->sml_numvals; i++) {
            out_mod.values.push_back(bv_to_string(in_mod->sml_values[i]));
        }
        out_mod.op = in_mod->sml_op;
        out_mod.flags = in_mod->sml_flags;
        op.mods.push_back(out_mod);
    }
}

void mod_op_add_entry(ModificationOp &op, const Entry &entry) {
    for (const Attribute *in_attr = entry.e_attrs; in_attr;
         in_attr = in_attr->a_next) {
        const string name = bv_to_string(in_attr->a_desc->ad_cname);
        vector<string> &attrs = op.entry[name];
        for (size_t i = 0; i < in_attr->a_numvals; i++) {
            attrs.push_back(bv_to_string(in_attr->a_vals[i]));
        }
    }
}

int mod_op_to_ldap(ModificationOp &op, Modifications **mods, string &error) {
    for (Modification &in_mod : op.mods) {
        auto out_mod =
            static_cast<Modifications *>(ch_calloc(1, sizeof(Modifications)));
        *mods = out_mod;
        mods = &out_mod->sml_next;

        BerValue name;
        name.bv_len = in_mod.name.size();
        name.bv_val = const_cast<char *>(in_mod.name.data());
        AttributeDescription *ad = nullptr;
        const char *text;
        int status = slap_bv2ad(&name, &ad, &text);
        if (status != LDAP_SUCCESS) {
            error = "Invalid attribute: " + in_mod.name;
            return status;
        }
        out_mod->sml_desc = ad;

        out_mod->sml_op = in_mod.op;
        out_mod->sml_flags = in_mod.flags;
        out_mod->sml_numvals = in_mod.values.size();
        out_mod->sml_values = static_cast<BerValue *>(
            ch_calloc(in_mod.values.size() + 1, sizeof(BerValue)));
        for (size_t i = 0; i < in_mod.values.size(); i++) {
            string_to_bv(in_mod.values[i], &out_mod->sml_values[i]);
        }
        BER_BVZERO(&out_mod->sml_values[in_mod.values.size()]);
        out_mod->sml_nvalues = nullptr;
    }

    return LDAP_SUCCESS;
}

//
// Hooks
//

int init_hook(BackendDB *be, ConfigReply *cr) {
    auto on = reinterpret_cast<slap_overinst *>(be->bd_info);
    on->on_bi.bi_private = InstanceInfo::create();
    return LDAP_SUCCESS;
}

int config_hook(BackendDB *be, const char *fname, int lineno, int argc,
                char **argv) {
    auto on = reinterpret_cast<slap_overinst *>(be->bd_info);
    auto info = static_cast<InstanceInfo *>(on->on_bi.bi_private);
    assert(info);

    string arg{argv[0]};
    if (arg == "py_filename") {
        if (argc == 2) {
            info->set_filename(argv[1]);
            return LDAP_SUCCESS;
        } else {
            Log2(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR,
                 "Wrong number of args for py_filename in %s on line %d\n",
                 fname, lineno);
            return LDAP_PARAM_ERROR;
        }
    } else if (arg == "py_function") {
        if (argc == 2) {
            info->set_function_name(argv[1]);
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

int open_hook(BackendDB *be, ConfigReply *cr) {
    auto on = reinterpret_cast<slap_overinst *>(be->bd_info);
    auto info = static_cast<InstanceInfo *>(on->on_bi.bi_private);
    assert(info);
    try {
        info->open();
    } catch (PyError &exc) {
        Log1(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR, "%s\n", exc.what());
        return LDAP_PARAM_ERROR;
    }
    return LDAP_SUCCESS;
}

int modify_hook(Operation *op, SlapReply *rs) {
    auto on = reinterpret_cast<slap_overinst *>(op->o_bd->bd_info);
    auto info = static_cast<InstanceInfo *>(on->on_bi.bi_private);
    assert(info);

    ModificationOp m2;
    mod_op_from_ldap(m2, op->o_req_ndn, op->o_authz.sai_ndn, op->orm_modlist);
    slap_mods_free(op->orm_modlist, 1);
    op->orm_modlist = nullptr;

    Entry *entry = nullptr;
    op->o_bd->bd_info = reinterpret_cast<BackendInfo *>(on->on_info);
    be_entry_get_rw(op, &op->o_req_ndn, nullptr, nullptr, 0, &entry);
    if (entry) {
        mod_op_add_entry(m2, *entry);
        be_entry_release_rw(op, entry, 0);
    }
    op->o_bd->bd_info = reinterpret_cast<BackendInfo *>(on);

    int status;
    string error;
    try {
        status = info->update(m2, error);
    } catch (PyError &exc) {
        Log1(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR, "%s\n", exc.what());
        status = LDAP_OTHER;
    }
    if (status != LDAP_SUCCESS) {
        op->o_bd->bd_info = reinterpret_cast<BackendInfo *>(on->on_info);
        send_ldap_error(op, rs, status, error.c_str());
        return status;
    }

    status = mod_op_to_ldap(m2, &op->orm_modlist, error);
    if (status != LDAP_SUCCESS) {
        slap_mods_free(op->orm_modlist, 1);
        op->orm_modlist = nullptr;
        op->o_bd->bd_info = reinterpret_cast<BackendInfo *>(on->on_info);
        send_ldap_error(op, rs, status, error.c_str());
        return status;
    }

    return SLAP_CB_CONTINUE;
}

int destroy_hook(BackendDB *be, ConfigReply *cr) {
    auto on = reinterpret_cast<slap_overinst *>(be->bd_info);
    auto info = static_cast<InstanceInfo *>(on->on_bi.bi_private);
    delete info;
    on->on_bi.bi_private = nullptr;
    return LDAP_SUCCESS;
}

}  // anonymous namespace
}  // namespace slapo_py_update_hook

extern "C" int init_module(int argc, char *argv[]) {
    try {
        slapo_py_update_hook::init_python();
    } catch (slapo_py_update_hook::PyError &exc) {
        Log1(LDAP_DEBUG_ANY, LDAP_LEVEL_ERR, "%s\n", exc.what());
        return LDAP_OTHER;
    }

    static slap_overinst overlay;
    memset(&overlay, 0, sizeof(overlay));
    overlay.on_bi.bi_type = const_cast<char *>("py_update_hook");
    overlay.on_bi.bi_db_init = &slapo_py_update_hook::init_hook;
    overlay.on_bi.bi_db_config = &slapo_py_update_hook::config_hook;
    overlay.on_bi.bi_db_open = &slapo_py_update_hook::open_hook;
    overlay.on_bi.bi_op_modify = &slapo_py_update_hook::modify_hook;
    overlay.on_bi.bi_db_destroy = &slapo_py_update_hook::destroy_hook;
    return overlay_register(&overlay);
}
