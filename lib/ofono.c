#include <dbus/dbus-glib.h>
#include <connui/connui-log.h>

#include "connui-cell-marshal.h"
#include "context.h"



// XXX: hack
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
// XXX: end hack


static void manager_valid_cb(OfonoManager* manager, void* arg);
static void modem_added_cb(OfonoManager* manager, OfonoModem* modem, void* arg);
static void modem_removed_cb(OfonoManager* manager, const char* path, void* arg);

static void set_modem(connui_cell_context *ctx, OfonoManager* manager, OfonoModem* modem);

void debug_sim(OfonoSimMgr* sim);
void sim_valid(OfonoSimMgr* sim, void* arg);
gulong sim_property_changed(OfonoSimMgr* sim, const char* name, GVariant *value, void* arg);

gboolean
register_ofono(connui_cell_context *ctx) {
    ctx->ofono_manager = NULL;
    ctx->ofono_modem = NULL;
    ctx->ofono_sim_manager = NULL;
    ctx->ofono_conn_manager = NULL;

    ctx->ofono_manager_valid_id = 0;
    ctx->ofono_sim_manager_valid_id = 0;
    ctx->ofono_modem_added_id = 0;
    ctx->ofono_modem_removed_id = 0;
    ctx->ofono_modem_path = NULL;

    // XXX: Free manager at some point, remove the handlers
    ctx->ofono_manager = ofono_manager_new();

    ctx->ofono_manager_valid_id = ofono_manager_add_valid_changed_handler(
            ctx->ofono_manager, manager_valid_cb, ctx);
	ctx->ofono_modem_added_id = ofono_manager_add_modem_added_handler(
			ctx->ofono_manager, modem_added_cb, ctx);
	ctx->ofono_modem_removed_id = ofono_manager_add_modem_removed_handler(
			ctx->ofono_manager, modem_removed_cb, ctx);

    CONNUI_ERR("ofono manager valid\n");

    return TRUE;
}

void unregister_ofono(connui_cell_context *ctx) {
    ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_manager_valid_id);
    ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_modem_added_id);
    ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_modem_removed_id);
    ofono_manager_unref(ctx->ofono_manager);
}

static void set_modem(connui_cell_context *ctx, OfonoManager* manager, OfonoModem* modem) {
    const char* path;

    ctx->ofono_modem = ofono_modem_ref(modem);
    path = ofono_modem_path(ctx->ofono_modem);
    CONNUI_ERR("MODEM PATH: %s", path);

    /* TODO: test if the modem supports sim and conn interfaces,
     * see ofono_modem_has_interface in
     * example libgofono/tools/ofono-sim/ofono-sim.c */

    ctx->ofono_modem_path = g_strdup(path);
    ctx->ofono_sim_manager = ofono_simmgr_new(path);
    ctx->ofono_conn_manager = ofono_connmgr_new(path);

    /* Connect sim valid and connmgr stuff to callbacks */

    ctx->ofono_sim_manager_valid_id = ofono_simmgr_add_valid_changed_handler(ctx->ofono_sim_manager, sim_valid, ctx);
    //ctx->ofono_sim_manager_valid_id = ofono_simmgr_add_property_changed_handler(ctx->ofono_sim_manager, sim_property_changed, NULL);
}

static void
manager_valid_cb(OfonoManager* manager, void* arg) {
    /* TODO: get modem list, pick first valid one, and use that for lifetime,
     * until we can manage modems, and pick up new ones as they appear and also
     * reconnect to modems if they disappear and reappear */
    GPtrArray *modems;
    guint i;

    CONNUI_ERR("ofono manager becomes valid");

    connui_cell_context* ctx = arg;

    /* TODO: Just use modem_new callback instead of this loop?
     * (Or call the same function) */
    modems = ofono_manager_get_modems(ctx->ofono_manager);
    for (i = 0; i < modems->len; i++) {
        /* XXX: turn this into add-modem or pick-modem function,
         * don't want more than one modem atm. */
        OfonoModem *modem = (OfonoModem*)modems->pdata[i];

        if (ofono_modem_valid(modem)) {
            CONNUI_ERR("MODEM VALID");
            set_modem(ctx, ctx->ofono_manager, modem);
			break;
        } else {
			CONNUI_ERR("MODEM NOT VALID");
		}
    }
}

static void
modem_added_cb(OfonoManager* manager, OfonoModem* modem, void* arg) {
	CONNUI_ERR("modem_added_cb");
}

static void
modem_removed_cb(OfonoManager* manager, const char* path, void* arg) {
	CONNUI_ERR("modem_removed_cb: %s", path);

    connui_cell_context *ctx = arg;

    if (!ctx->ofono_modem_path)
        return;

    // Check if it's "our" modem (by path)
    if (!strcmp(path, ctx->ofono_modem_path)) {
        // TODO: turn this into a func
        g_free(ctx->ofono_modem_path);

        ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_sim_manager_valid_id);

        ofono_connmgr_unref(ctx->ofono_conn_manager);
        ofono_simmgr_unref(ctx->ofono_sim_manager);
        ofono_modem_unref(ctx->ofono_modem);

        ctx->ofono_modem = NULL;
        ctx->ofono_sim_manager = NULL;
        ctx->ofono_conn_manager = NULL;
        ctx->ofono_sim_manager_valid_id = 0;
        ctx->ofono_modem_added_id = 0;
        ctx->ofono_modem_path = NULL;
    }

    /* Match if it is the one we currently use, if so, hurr... reset? */
}

/* TODO: move out of context? */
void debug_sim(OfonoSimMgr* sim) {
	CONNUI_ERR("debug_sim");
	guint i;
    OfonoObject* obj = ofono_simmgr_object(sim);
    GPtrArray* keys = ofono_object_get_property_keys(obj);
	CONNUI_ERR("debug_sim keys len %d", keys->len);
    for (i=0; i<keys->len; i++) {
        const char* key = keys->pdata[i];
        if (1) {
            GVariant* v = ofono_object_get_property(obj, key, NULL);
            gchar* text = g_variant_print(v, FALSE);
            CONNUI_ERR("%s: %s\n", key, text);
            g_free(text);
        } else {
            CONNUI_ERR("%s\n", key);
        }
    }
	CONNUI_ERR("debug_sim_done");
}

void sim_valid(OfonoSimMgr* sim, void* arg) {
    connui_cell_context* ctx = arg;

	CONNUI_ERR("sim_valid");
    if (ctx->sim_status_cbs) {
        //connui_utils_notify_notify_UINT(ctx->sim_status_cbs, 1);
    }

	if (sim->intf.object.valid) {
		debug_sim(sim);
    
	}
}

gulong sim_property_changed(OfonoSimMgr* sim, const char* name, GVariant *value, void* arg) {
    gchar* varstr;

	CONNUI_ERR("sim_property_changed:", name);
    // XXX: this might not be correct code
    g_variant_get(value, "s", &varstr);
	g_print("Val: %s\n", varstr);
    g_free(varstr);

    return 0; // XXX
}


