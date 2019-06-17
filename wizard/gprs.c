#include <connui/connui.h>
#include <connui/iapsettings/stage.h>
#include <connui/iapsettings/mapper.h>
#include <connui/iapsettings/widgets.h>
#include <connui/iapsettings/wizard.h>

#include <string.h>
#include <libintl.h>

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

struct _iap_gprs_private
{
  struct iap_wizard *iw;
  struct iap_wizard_plugin *plugin;
  struct stage stage;
};

typedef struct _iap_gprs_private iap_gprs_private;

struct gprs_page_field
{
  const char *id;
  const char *caption_id;
  const char *msgid;
};

static gboolean
iap_wizard_plugin_gprs_stage_widget_validate(const struct stage *s,
                                             const gchar *name,
                                             const gchar *key)
{
  gchar *type = stage_get_string(s, "type");
  gboolean rv;

  if (type)
    rv = !strncmp(type, "GPRS", 4);
  else
    rv = FALSE;

  g_free(type);

  return rv;
}

static struct stage_widget iap_wizard_plugin_gprs_stage_widgets[] =
{
  {
    NULL,
    &iap_wizard_plugin_gprs_stage_widget_validate,
    "GPRS_AP_NAME",
    "gprs_accesspointname",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    &iap_wizard_plugin_gprs_stage_widget_validate,
    "GPRS_USERNAME",
    "gprs_username",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    &iap_wizard_plugin_gprs_stage_widget_validate,
    "GPRS_PASSWORD",
    "gprs_password",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    &iap_wizard_plugin_gprs_stage_widget_validate,
    "GPRS_ASK_PASSWORD",
    "ask_password",
    NULL,
    &mapper_toggle2bool,
    NULL
  },
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

static void
ask_password_toggled_cb(GtkToggleButton *togglebutton, gpointer user_data)
{
  gtk_widget_set_sensitive(GTK_WIDGET(user_data),
                           !gtk_toggle_button_get_active(togglebutton));
}

static GtkWidget *
iap_wizard_plugin_gprs_page_create(gpointer user_data)
{
  iap_gprs_private *priv = user_data;
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkSizeGroup *sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  const struct gprs_page_field fields[] =
  {
    { "GPRS_AP_NAME", "GPRS_AP_NAME_CAPTION", "conn_set_iap_fi_accessp_name" },
    { "GPRS_USERNAME", NULL, "conn_set_iap_fi_username" },
    { "GPRS_PASSWORD", "GPRS_PASSWORD_CAPTION", "conn_set_iap_fi_password" }
  };
  GtkWidget *caption, *button;
  GtkEntry *entry;
  int i;

  for (i = 0; i < G_N_ELEMENTS(fields); i++)
  {
    HildonGtkInputMode im;
    GtkWidget *widget = gtk_entry_new();

    im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(widget));
    im &= ~HILDON_GTK_INPUT_MODE_AUTOCAP;
    hildon_gtk_entry_set_input_mode(GTK_ENTRY(widget), im);
    g_hash_table_insert(priv->plugin->widgets, g_strdup(fields[i].id), widget);

    caption = hildon_caption_new(sizegroup, _(fields[i].msgid), widget, NULL,
                                 HILDON_CAPTION_OPTIONAL);

    if (fields[i].caption_id)
    {
      g_hash_table_insert(priv->plugin->widgets, g_strdup(fields[i].caption_id),
                          caption);
    }

    gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  }

  entry =
      GTK_ENTRY(g_hash_table_lookup(priv->plugin->widgets, "GPRS_PASSWORD"));
  gtk_entry_set_visibility(entry, FALSE);
  gtk_entry_set_max_length(entry, 256);

  button = gtk_check_button_new();
  g_hash_table_insert(priv->plugin->widgets, g_strdup("GPRS_ASK_PASSWORD"),
                      button);

  g_signal_connect(G_OBJECT(button), "toggled",
                   G_CALLBACK(ask_password_toggled_cb),
                   g_hash_table_lookup(priv->plugin->widgets,
                                       "GPRS_PASSWORD_CAPTION"));

  caption = hildon_caption_new(NULL, _("conn_set_iap_fi_ask_pw_every"),
                               button, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  g_object_unref(G_OBJECT(sizegroup));

  return vbox;
}

static const char *
iap_wizard_plugin_gprs_page_get_page(gpointer user_data, gboolean show_note)
{
  return "COMPLETE";
}

static struct iap_wizard_page iap_wizard_plugin_gprs_pages[] =
{
  {
    "GPRS",
    "conn_set_iap_ti_packet_data",
    iap_wizard_plugin_gprs_page_create,
    iap_wizard_plugin_gprs_page_get_page,
    NULL,
    NULL,
    NULL,
    "Connectivity_Internetsettings_IAPsetupPSD",
    NULL
  },
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

static const char *
iap_wizard_plugin_gprs_get_page(gpointer user_data, int index,
                                gboolean show_note)
{
  iap_gprs_private *priv = user_data;
  struct iap_wizard *iw = priv->iw;
  const struct stage *s = iap_wizard_get_active_stage(priv->iw);

  if (!s && index == -1)
    return NULL;

  if (index != -1)
  {
    GtkWidget *widget = iap_wizard_get_widget(iw, "NAME");
    gchar *text = (gchar *)gtk_entry_get_text(GTK_ENTRY(widget));

    if (text && *text && !iap_settings_is_empty(text))
    {
      iap_wizard_set_active_stage(iw, &priv->stage);
      return "GPRS";
    }

    if (show_note)
    {
      GtkWidget *dialog = iap_wizard_get_dialog(iw);

      hildon_banner_show_information(GTK_WIDGET(dialog), NULL,
                                     _("conn_ib_enter_name"));
      gtk_widget_grab_focus(widget);
    }
  }
  else
  {
    gchar *type = stage_get_string(s, "type");

    if (type && !strncmp(type, "GPRS", 4))
    {
      iap_wizard_select_plugin_label(iw, "GPRS", 0);

      if (s != &priv->stage)
      {
          stage_copy(s, &priv->stage);
          iap_wizard_set_active_stage(iw, &priv->stage);
      }

      g_free(type);
      return "GPRS";
    }

    g_free(type);
  }

  return NULL;
}

static const gchar **
iap_wizard_plugin_gprs_get_widgets(gpointer user_data)
{
  iap_gprs_private *priv = user_data;
  struct stage *s = iap_wizard_get_active_stage(priv->iw);
  gchar *type = NULL;
  static const gchar *widgets[2];
  static gboolean first_time = TRUE;

  if (s)
    type = stage_get_string(s, "type");

  if (first_time || (type && !strncmp(type, "GPRS", 4)))
    widgets[0] = _("conn_set_iap_fi_psd");
  else
    widgets[0] = NULL;

  first_time = FALSE;
  g_free(type);

  return widgets;
}

static void
iap_wizard_plugin_gprs_save_state(gpointer user_data, GByteArray *state)
{
  iap_gprs_private *priv = user_data;
  stage_dump_cache(&priv->stage, state);
}

static void
iap_wizard_plugin_gprs_restore(gpointer user_data, struct stage_cache *cache)
{
  iap_gprs_private *priv = user_data;

  stage_restore_cache(&priv->stage, cache);
}

gboolean
iap_wizard_plugin_init(struct iap_wizard *iw,
                       struct iap_wizard_plugin *plugin)
{
  iap_gprs_private *priv = g_new0(iap_gprs_private, 1);
  struct stage *s = &priv->stage;

  priv->iw = iw;
  priv->plugin = plugin;

  stage_create_cache(s, NULL);
  priv->stage.name = g_strdup("GPRS");
  stage_set_string(s, "type", "GPRS");
  stage_set_string(s, "gprs_accesspointname", "internet");
  plugin->name = "GPRS";
  plugin->prio = 600;
  plugin->get_page = iap_wizard_plugin_gprs_get_page;
  plugin->stage_widgets = iap_wizard_plugin_gprs_stage_widgets;
  plugin->pages = iap_wizard_plugin_gprs_pages;
  plugin->get_widgets = iap_wizard_plugin_gprs_get_widgets;
  plugin->save_state = iap_wizard_plugin_gprs_save_state;
  plugin->restore = iap_wizard_plugin_gprs_restore;
  plugin->widgets =
          g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  plugin->priv = priv;

  return TRUE;
}

void
iap_wizard_plugin_destroy(struct iap_wizard *iw,
                          struct iap_wizard_plugin *plugin)
{
  iap_gprs_private *priv = plugin->priv;

  stage_free(&priv->stage);
  g_hash_table_destroy(plugin->widgets);
  g_free(plugin->priv);
}
