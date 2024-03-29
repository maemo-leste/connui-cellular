#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include <string.h>

#include "context.h"
#include "ofono-context.h"

/*
 * Sim status list:
 * 1, 0 = sim available, no pin required
 * 7, 0 = sim available, pin not yet entered
 *
 * Presumably, but have to test:
 * 8 = puk required to be entered (still unrelated to sim lock?)
 */

/* TODO: example of getter, rather than property changed handler */
void debug_sim(OfonoSimMgr* sim) {
	CONNUI_ERR("debug_sim");
	guint i;
    OfonoObject* obj = ofono_simmgr_object(sim);

    /* XXX: Shouldn't these keys be freed ? */
    GPtrArray* keys = ofono_object_get_property_keys(obj);
	CONNUI_ERR("debug_sim keys len %d", keys->len);
    for (i=0; i<keys->len; i++) {
        const char* key = keys->pdata[i];
        if (1) {
            /* XXX: shouldn't these gvariants be freed / dereffed? */
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

void present_changed(OfonoSimMgr* sender, void* arg) {
    guint present_status;

    CONNUI_ERR("** present changed");

    connui_cell_context *ctx = arg;
    // XXX: free obj? deref obj? nothing?
    OfonoObject* obj = ofono_simmgr_object(ctx->ofono_sim_manager);

    // XXX: deref variant?
    GVariant* v = ofono_object_get_property(obj, "Present", NULL);
    gboolean present;
    g_variant_get(v, "b", &present);
    //g_variant_unref(v);

    CONNUI_ERR("** present: %d", present);


    if (present) {
        if (connui_cell_sim_needs_pin(NULL)) {
            present_status = 7;
        } else {
            present_status = 1;
        }
    }
    else
        present_status = 0;

    CONNUI_ERR("Sending present notification with %d", present_status);
    /* TODO: Perhaps figure out all the different statuses of the sim and pass
     * that here right away -- like is the sim locked or not, etc, Also see
     * connui_cell_code_ui_sim_status_cb */
    connui_utils_notify_notify_UINT(ctx->sim_status_cbs, present_status);
}

void pinrequired_changed(OfonoSimMgr* sender, void* arg) {
    CONNUI_ERR("**pinrequired changed");
    connui_cell_context *ctx = arg;
    debug_sim(ctx->ofono_sim_manager);
    CONNUI_ERR("**pinrequired changed done");

    /* Send notification of changed status? */
    // XXX: HACK: for now just call present_changed to send new sim status
    present_changed(ctx->ofono_sim_manager, ctx);
}

void set_sim(connui_cell_context *ctx) {
    CONNUI_ERR("set_sim");

    /* TODO: do we want this here too? maybe we don't need it */
    if ((ctx->ofono_sim_manager) && (ctx->ofono_sim_present_changed_valid_id)) {
        ofono_simmgr_remove_handler(ctx->ofono_sim_manager, ctx->ofono_sim_present_changed_valid_id);
        ctx->ofono_sim_present_changed_valid_id = 0;
        ctx->ofono_sim_pinrequired_changed_valid_id = 0;
    }

    ctx->ofono_sim_present_changed_valid_id = ofono_simmgr_add_present_changed_handler(ctx->ofono_sim_manager,
            present_changed, ctx);
    ctx->ofono_sim_pinrequired_changed_valid_id = ofono_simmgr_add_pin_required_changed_handler(ctx->ofono_sim_manager,
            pinrequired_changed, ctx);

    /* XXX: first manual invoke */
    present_changed(ctx->ofono_sim_manager, ctx);

    return;
}

void release_sim(connui_cell_context *ctx) {
    CONNUI_ERR("release_sim");

    if ((ctx->ofono_sim_manager) && (ctx->ofono_sim_present_changed_valid_id)) {
        ofono_simmgr_remove_handler(ctx->ofono_sim_manager, ctx->ofono_sim_present_changed_valid_id);
        ctx->ofono_sim_present_changed_valid_id = 0;
    }

    if ((ctx->ofono_sim_manager) && (ctx->ofono_sim_pinrequired_changed_valid_id)) {
        ofono_simmgr_remove_handler(ctx->ofono_sim_manager, ctx->ofono_sim_pinrequired_changed_valid_id);
        ctx->ofono_sim_pinrequired_changed_valid_id = 0;
    }

    return;
}


void
connui_cell_sim_status_close(cell_sim_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->sim_status_cbs = connui_utils_notify_remove(ctx->sim_status_cbs, cb);

  if (!ctx->sim_status_cbs) {
      /* TODO MW: maybe unset any callbacks/signals, but probably not, and then
       * the if statement can just go*/
  }

  connui_cell_context_destroy(ctx);
}

/* TODO: this is currently used in lib/security_code.c in a hacky manner, but
 * this code doesn't work anymore, and should go. So lib/security_code.c should
 * be modified to make it work with the newer and non-hacky API. I will do that
 * once I can test it, I don't want to write code that I cannot test, as it'll
 * be guaranteed to be wrong. */
typedef void (*get_sim_status_cb_f)(DBusGProxy *, guint, gint, GError *,
                                    connui_cell_context *);
__attribute__((visibility("hidden"))) void
get_sim_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = (sim_status_data *)user_data;
  gint error_value;
  guint sim_status;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UINT, &sim_status,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);
  ((get_sim_status_cb_f)data->cb)(proxy, sim_status, error_value, error,
                                  data->data);
}

static gboolean issue_updates(gpointer user_data) {
  connui_cell_context *ctx = connui_cell_context_get();
  present_changed(ctx->ofono_sim_manager, ctx);
  connui_cell_context_destroy(ctx);
  return TRUE;
}

gboolean
connui_cell_sim_status_register(cell_sim_status_cb cb, gpointer user_data)
{
  CONNUI_ERR("connui_cell_sim_status_register");

  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  ctx->sim_status_cbs =
      connui_utils_notify_add(ctx->sim_status_cbs, cb, user_data);

  if ((ctx->ofono_sim_manager) && (ofono_simmgr_valid(ctx->ofono_sim_manager))) {
      /* XXX: this is a hack */
      issue_updates(NULL);
  }

  connui_cell_context_destroy(ctx);

  return TRUE;
}

gboolean
connui_cell_sim_is_network_in_service_provider_info(gint *error_value,
                                                    guchar *code)
{
  gboolean rv = FALSE;
  GArray *provider_info = NULL;
  GError *error = NULL;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (dbus_g_proxy_call(
        ctx->phone_sim_proxy, "get_service_provider_info", &error,
        G_TYPE_INVALID,
        dbus_g_type_get_collection("GArray", G_TYPE_UCHAR), &provider_info,
        G_TYPE_INT, error_value,
        G_TYPE_INVALID))
  {
    if (provider_info)
    {
      int i;

      for (i = 0; i < provider_info->len; i += 4)
      {
        if (!memcmp(&provider_info->data[i], code, 3))
        {
          rv = TRUE;
          break;
        }
      }

      g_array_free(provider_info, TRUE);
    }

    connui_cell_context_destroy(ctx);
  }
  else
  {
    CONNUI_ERR("Error with DBUS in: %s", error->message);
    g_clear_error(&error);

    if (error_value )
      *error_value = 1;

    connui_cell_context_destroy(ctx);
  }

  return rv;
}

gchar *
connui_cell_sim_get_service_provider(guint *name_type, gint *error_value)
{
    OfonoObject* obj;
    GVariant *v;
    gchar *name = NULL;

    CONNUI_ERR("connui_cell_sim_get_service_provider");

    connui_cell_context *ctx = connui_cell_context_get();
    g_return_val_if_fail(ctx != NULL, FALSE);

    // XXX: free obj? deref obj? nothing?
    obj = ofono_simmgr_object(ctx->ofono_sim_manager);
    v = ofono_object_get_property(obj, "ServiceProviderName", NULL);

    if (!v) {
        // Modem might not be online.
        CONNUI_ERR("Variant for ServiceProviderName is NULL");
    } else {
        g_variant_get(v, "s", &name);
        //g_variant_unref(v);
  }

    if (!name)
        *error_value = 1;

    connui_cell_context_destroy(ctx);

    return name;
}

gboolean connui_cell_sim_has_card_identifier() {
    OfonoObject* obj;
    GVariant *v;

    connui_cell_context *ctx = connui_cell_context_get();
    g_return_val_if_fail(ctx != NULL, FALSE);

    obj = ofono_simmgr_object(ctx->ofono_sim_manager);
    v = ofono_object_get_property(obj, "CardIdentifier", NULL);

    CONNUI_ERR("connui_cell_sim_has_card_identifier got CardIdentifier field");

    if (v) {
        char* identifier = NULL;
        g_variant_get(v, "s", &identifier);
        fprintf(stderr, "identifier: %s\n", identifier);
        g_free(identifier);
    }

    if (!v) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/* TODO: Returns if a sim present needs a pin
 * We will probably need it as well for puk at some point
 * previously if a pin was required was reflected in the get_sim_status dbus
 * call, but we need to figure it out for eourselves here.
 */
gboolean
connui_cell_sim_needs_pin(gboolean *has_error)
{
    /* TODO: pinrequired can contain a lot, we need to support more:
     * https://git.kernel.org/pub/scm/network/ofono/ofono.git/tree/doc/sim-api.txt
     */
    OfonoObject* obj;
    GVariant *v;
    char* pin_required;
    gboolean needed = FALSE;

    CONNUI_ERR("connui_cell_sim_needs_pin");

    connui_cell_context *ctx = connui_cell_context_get();
    g_return_val_if_fail(ctx != NULL, FALSE);

    obj = ofono_simmgr_object(ctx->ofono_sim_manager);
    v = ofono_object_get_property(obj, "PinRequired", NULL);

    CONNUI_ERR("connui_cell_sim_needs_pin got PinRequired field");

    if (!v) {
        if (has_error)
            *has_error = TRUE;
    } else {
        g_variant_get(v, "s", &pin_required);
        fprintf(stderr, "connui_cell_sim_needs_pin: %s\n", pin_required);

        needed = (strcmp(pin_required, "none") != 0);
        g_free(pin_required);

        if (has_error)
            *has_error = FALSE;
    }

    CONNUI_ERR("connui_cell_sim_needs_pin needed: %d", needed);

    return needed;
}

gboolean
connui_cell_sim_is_locked(gboolean *has_error)
{
    /* XXX: Code ifdef's below should be OK, but I can't test it right now */
    if (has_error) {
        *has_error = FALSE;
    }
    return FALSE;
#if 0
    // TODO: this relies on ctx->ofono_sim_manager being valid, check for that!
    /* TODO: Port to LockedPins, test with other sim cards.
     * enter pin and such is in lib/security-code*/
    OfonoObject* obj;
    GVariant *v;
    GVariantIter i;
    char* locked_pin;
    gboolean locked = FALSE;

    CONNUI_ERR("connui_cell_sim_is_locked");

    connui_cell_context *ctx = connui_cell_context_get();
    g_return_val_if_fail(ctx != NULL, FALSE);

    obj = ofono_simmgr_object(ctx->ofono_sim_manager);
    v = ofono_object_get_property(obj, "LockedPins", NULL);

    if (!v) {
        if (has_error)
            *has_error = TRUE;
    } else {
        g_variant_iter_init(&i, v);
        while (g_variant_iter_loop(&i, "s", &locked_pin)) {
            /* XXX: Assume that any value currently means pin is locked, since the
             * function has no argument for specific pins.
             * In the future we can check for specific strings/types */
            locked = TRUE;
            /* TODO: maybe need to free locked_pin every time? */
        }
        //g_variant_unref(v);

        if (has_error)
            *has_error = FALSE;
    }

    return locked;
#endif
}

guint
connui_cell_sim_get_status()
{
    /* XXX: DRY against present_changed */
    fprintf(stderr, "conn_cell_sim_get_status\n");
    guint present_status;
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_val_if_fail(ctx != NULL, FALSE);

    OfonoObject* obj = ofono_simmgr_object(ctx->ofono_sim_manager);

    GVariant* v = ofono_object_get_property(obj, "Present", NULL);
    gboolean present;
    g_variant_get(v, "b", &present);

    CONNUI_ERR("** present: %d", present);


    if (present) {
        if (connui_cell_sim_needs_pin(NULL)) {
            present_status = 7;
        } else {
            present_status = 1;
        }
    }
    else
        present_status = 0;

    return present_status;
}

gboolean
connui_cell_sim_deactivate_lock(const gchar *pin_code, gint *error_value)
{
    gboolean ok = FALSE;

    connui_cell_context *ctx = connui_cell_context_get();
    g_return_val_if_fail(ctx != NULL, FALSE);

    /* XXX: detect pin type, since function doesn't show it, or just pick "pin"
     * (and not "pin2") for now? */
    ok = ofono_simmgr_unlock_pin(ctx->ofono_sim_manager, "pin", pin_code);

    if (!ok && error_value) {
        *error_value = TRUE;
    }

    connui_cell_context_destroy(ctx);

    return ok;
}

guint
connui_cell_sim_verify_attempts_left(guint code_type, gint *error_value)
{
    /* TODO: Use `Retries` property for the right code type, see
     * include/connui-cellular.h for the SIM code types - SIM_SECURITY_CODE_PIN
     * and other security_code_type ; map to ofono types. */

    connui_cell_context *ctx = connui_cell_context_get();
    guchar attempts_left = 0;

    g_return_val_if_fail(ctx != NULL, attempts_left);

    if (!ctx->ofono_sim_manager) {
        *error_value = 1;
        goto cleanup;
    }

    // XXX: free obj? deref obj? nothing?
    OfonoObject* obj = ofono_simmgr_object(ctx->ofono_sim_manager);

    GVariant* v = ofono_object_get_property(obj, "Retries", NULL);
    if (!v) {
        *error_value = 1;
        goto cleanup;
    }

    GVariant *v2;

    switch (code_type) {
        case SIM_SECURITY_CODE_PIN:
            v2 = g_variant_lookup_value(v, "pin", G_VARIANT_TYPE_BYTE);
            break;
        case SIM_SECURITY_CODE_PUK:
            v2 = g_variant_lookup_value(v, "puk", G_VARIANT_TYPE_BYTE);
            break;
        case SIM_SECURITY_CODE_PIN2:
            v2 = g_variant_lookup_value(v, "pin2", G_VARIANT_TYPE_BYTE);
            break;
        /* TODO: UPIN, UPUK == puk2? */
        default:
            CONNUI_ERR("Invalid code_type: %d", code_type);
            *error_value = 1;
            goto cleanup;
    }

    g_variant_get(v2, "y", &attempts_left);
    g_variant_unref(v2);

    *error_value = 0;

    cleanup:
    connui_cell_context_destroy(ctx);

    return (guint)attempts_left;
}
