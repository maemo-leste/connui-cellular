#include <dbus/dbus-glib.h>
#include <connui/connui-log.h>

#include "connui-cell-marshal.h"
#include "context.h"
#include "ofono-context.h"


#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>

#include "sim.h"
#include "net.h"

static void
add_modem(connui_cell_context *ctx, OfonoManager* manager, OfonoModem* modem)
{
  const char *path = ofono_modem_path(modem);

  CONNUI_ERR("ADD MODEM PATH: %s", path);

  g_hash_table_insert(ctx->modems, g_strdup(path), ofono_modem_ref(modem));

  connui_cell_sim_add_modem(ctx, modem);
  connui_cell_net_add_modem(ctx, modem);
}

static void
manager_valid_cb(OfonoManager *manager, void *arg)
{
  connui_cell_context *ctx = arg;
  GPtrArray *modems = ofono_manager_get_modems(ctx->ofono_manager);
  guint i;

  CONNUI_ERR("ofono manager becomes valid");

  for (i = 0; i < modems->len; i++)
    add_modem(ctx, ctx->ofono_manager, modems->pdata[i]);
}

static void
modem_added_cb(OfonoManager* manager, OfonoModem* modem, void* arg)
{
    add_modem(arg, manager, modem);
}

static void
modem_removed_cb(OfonoManager *manager, const char *path, void *arg)
{
  CONNUI_ERR("modem_removed_cb: %s", path);

  connui_cell_context *ctx = arg;

  g_hash_table_remove(ctx->modems, path);
}

gboolean
register_ofono(connui_cell_context *ctx)
{
  ctx->modems = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                      (GDestroyNotify)ofono_modem_unref);
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

void
unregister_ofono(connui_cell_context *ctx)
{
  g_hash_table_unref(ctx->modems);
  ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_manager_valid_id);
  ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_modem_added_id);
  ofono_manager_remove_handler(ctx->ofono_manager, ctx->ofono_modem_removed_id);
  ofono_manager_unref(ctx->ofono_manager);
}
