#include <hildon/hildon.h>
#include <connui/connui-log.h>
#include <connui/iap-common.h>

#include <libintl.h>
#include <time.h>
#include <string.h>

#include "connui-cellular.h"
#include "data-counter.h"

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

static gboolean dc_unknown_bool_1 = FALSE;
static GtkWidget *data_counters_dialog = NULL;
static GtkWidget *dc_sent = NULL;
static GtkWidget *dc_received = NULL;
static GtkWidget *dc_reset = NULL;
static GtkWidget *dc_reset_button = NULL;
static GtkWidget *dc_save_button = NULL;
static GtkWidget *dc_limit_entry = NULL;
static GtkWidget *dc_enable_warning_button = NULL;
static time_t reset_time;
static struct
{
  const char *msgid;
  const char *fmt;
}
datacounter_msgid[] =
{
  {"conn_fi_received_sent_byte", "%s B" },
  {"conn_fi_received_sent_kilobyte", "%s kB" },
  {"conn_fi_received_sent_megabyte", "%s MB" },
  {"conn_fi_received_sent_gigabyte", "%s GB" },
  {NULL, NULL}
};

static gchar *
format_data_counter(float val, int pow)
{
  if (pow < 0)
    return NULL;

  if (!pow)
    return g_strdup_printf("%3g", val);

  if (val < 10.0)
    return g_strdup_printf("%1.2f", val);

  if (val < 100.0)
    return g_strdup_printf("%2.1f", val);

  if (val >= 1000.0)
    return g_strdup_printf("%.0f", val);

  return g_strdup_printf("%3.f", val);
}

static void
cellular_data_counter_update_cb(guint64 rx_bytes, guint64 tx_bytes,
                                time_t reset_time,
                                gboolean notification_enabled,
                                const gchar *warning_limit, gpointer user_data)
{
  int i = 0;
  float rx = rx_bytes;
  float tx = tx_bytes;
  gchar *s;
  gchar *rx_fmt;
  gchar *tx_fmt;
  char time_buf[200];

  while ((rx >= 1000.0 || tx >= 1000.0) && datacounter_msgid[i + 1].msgid)
  {
    rx /= 1000.0;
    tx /= 1000.0;
    i++;
  }

  s = format_data_counter(rx, i);
  rx_fmt = g_strdup_printf(_(datacounter_msgid[i].msgid), s);
  g_free(s);

  s = format_data_counter(tx, i);
  tx_fmt = g_strdup_printf(_(datacounter_msgid[i].msgid), s);
  g_free(s);

  gtk_label_set_text(GTK_LABEL(dc_sent), tx_fmt);
  gtk_label_set_text(GTK_LABEL(dc_received), rx_fmt);

  g_free(rx_fmt);
  g_free(tx_fmt);

  if (reset_time)
  {
    if (strftime(time_buf, sizeof(time_buf), _("conn_fi_phone_dc_reset_time"),
                 localtime(&reset_time)))
    {
      gtk_label_set_text(GTK_LABEL(dc_reset), time_buf);
    }
    else
      CONNUI_ERR("Unable to set reset date");
  }
  else
    gtk_label_set_text(GTK_LABEL(dc_reset), _("conn_va_phone_dc_never"));

  if (dc_unknown_bool_1)
  {
    if (warning_limit)
      hildon_entry_set_text(HILDON_ENTRY(dc_limit_entry), warning_limit);

    hildon_check_button_set_active(
          HILDON_CHECK_BUTTON(dc_enable_warning_button), notification_enabled);
    gtk_widget_set_sensitive(dc_limit_entry, notification_enabled);
    dc_unknown_bool_1 = 0;
  }
}

void
cellular_data_counter_destroy()
{
  if (data_counters_dialog)
  {
    connui_cell_datacounter_close(cellular_data_counter_update_cb);
    gtk_widget_destroy(data_counters_dialog);
    data_counters_dialog = NULL;
  }
}

void
cellular_data_counter_save()
{
  gboolean notification_enabled = hildon_check_button_get_active(
        HILDON_CHECK_BUTTON(dc_enable_warning_button));
  gchar *warning_limit =
      g_strdup(hildon_entry_get_text(HILDON_ENTRY(dc_limit_entry)));

  connui_cell_datacounter_save(notification_enabled, warning_limit);
  g_free(warning_limit);
}

void
cellular_data_counter_reset()
{
  connui_cell_datacounter_reset();
}

static GtkWidget *
dc_create_counter(GtkWidget *parent, GtkSizeGroup *size_group,
                  const char *title)
{
  GtkWidget *label = gtk_label_new(title);
  GtkWidget *rv = gtk_label_new("");
  GtkWidget *hbox;

  gtk_size_group_add_widget(size_group, label);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  gtk_misc_set_alignment(GTK_MISC(rv), 0.0, 0.0);
  hildon_helper_set_logical_color(label, GTK_RC_FG, 0, "SecondaryTextColor");

  hbox = gtk_hbox_new(FALSE, 16);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), rv, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, FALSE, 0);

  return rv;
}

static void
dc_enable_warning_button_toggled_cb(HildonCheckButton *button,
                                    gpointer user_data)
{
  if (hildon_check_button_get_active(button))
    gtk_widget_set_sensitive(dc_limit_entry, TRUE);
  else
    gtk_widget_set_sensitive(dc_limit_entry, FALSE);
}

static void
dc_limit_entry_chagned_cb(GtkEntry *entry, gpointer user_data)
{
  if (strlen(gtk_entry_get_text(GTK_ENTRY(entry))) )
    gtk_widget_set_sensitive(dc_save_button, TRUE);
  else
    gtk_widget_set_sensitive(dc_save_button, FALSE);
}

static void
dc_limit_entry_insert_text_cb(GtkEditable *editable, gchar *new_text,
                              gint new_text_length, gpointer position,
                              gpointer user_data)
{
  glong len = g_utf8_strlen(new_text, -1);
  const gchar *text = hildon_entry_get_text(HILDON_ENTRY(dc_limit_entry));

  if (g_utf8_strlen(text, -1) + len > 6)
    hildon_banner_show_information(NULL, NULL, _("conn_ib_maxval_reach"));
  else
  {
    int i = 0;

    if (new_text_length <= 0)
      return;

    while (new_text[i] != '-')
    {
      if (i == 0 && *((gint *)position) == 0 && new_text[i] == '0')
        break;

      i++;

      if (i == new_text_length)
        return;
    }
  }

  g_signal_stop_emission_by_name(G_OBJECT(editable), "insert_text");
}

GtkDialog *
cellular_data_counter_show(GtkWindow *parent, gboolean home_counter)
{
  const char *title;
  GtkSizeGroup *size_group;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *warning_label;
  GtkWidget *limit_mb_label;

  if (data_counters_dialog)
  {
    gtk_widget_show_all(data_counters_dialog);
    return GTK_DIALOG(data_counters_dialog);
  }

  if (home_counter)
    title = _("conn_ti_phone_data_counter");
  else
    title = _("conn_ti_phone_roaming_data_counter");

  data_counters_dialog = gtk_dialog_new_with_buttons(
        title, parent,
        GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_DESTROY_WITH_PARENT |
        GTK_DIALOG_MODAL,
        NULL);
  size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  vbox = GTK_DIALOG(data_counters_dialog)->vbox;
  dc_sent = dc_create_counter(vbox, size_group, _("conn_fi_phone_dc_sent"));
  dc_received = dc_create_counter(vbox, size_group,
                                  _("conn_fi_phone_dc_received"));
  dc_reset = dc_create_counter(vbox, size_group, _("conn_fi_phone_dc_reset"));

  dc_enable_warning_button = hildon_check_button_new(HILDON_SIZE_FINGER_HEIGHT);
  g_signal_connect(G_OBJECT(dc_enable_warning_button), "toggled",
                   G_CALLBACK(dc_enable_warning_button_toggled_cb), NULL);

  gtk_button_set_label(GTK_BUTTON(dc_enable_warning_button),
                       _("conn_bd_phone_limit_enable_warning"));

  gtk_box_pack_start(GTK_BOX(vbox), dc_enable_warning_button, FALSE, FALSE, 0);

  dc_limit_entry = hildon_entry_new(HILDON_SIZE_FINGER_HEIGHT);
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(dc_limit_entry),
                                  HILDON_GTK_INPUT_MODE_NUMERIC);
  gtk_entry_set_width_chars(GTK_ENTRY(dc_limit_entry), 6);
  gtk_entry_set_max_length(GTK_ENTRY(dc_limit_entry), 6);
  g_signal_connect(G_OBJECT(dc_limit_entry), "insert_text",
                   G_CALLBACK(dc_limit_entry_insert_text_cb), NULL);
  g_signal_connect(G_OBJECT(dc_limit_entry), "changed",
                   G_CALLBACK(dc_limit_entry_chagned_cb), NULL);

  hbox = gtk_hbox_new(FALSE, 8);
  warning_label = gtk_label_new(_("conn_fi_phone_limit_display_warning"));
  hildon_helper_set_logical_color(warning_label, GTK_RC_FG, 0,
                                  "SecondaryTextColor");

  limit_mb_label = gtk_label_new(_("conn_fi_phone_limit_megabyte"));
  hildon_helper_set_logical_color(limit_mb_label, GTK_RC_FG, 0,
                                  "SecondaryTextColor");

  gtk_misc_set_alignment(GTK_MISC(warning_label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(hbox), warning_label, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), dc_limit_entry, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(hbox), limit_mb_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  dc_reset_button = gtk_dialog_add_button(GTK_DIALOG(data_counters_dialog),
                                          _("conn_bd_phone_dc_reset"), 1);

  dc_save_button = gtk_dialog_add_button(
        GTK_DIALOG(data_counters_dialog),
        dgettext("hildon-libs", "wdgt_bd_save"), 2);

  iap_common_set_close_response(data_counters_dialog, GTK_RESPONSE_CANCEL);
  g_object_unref(size_group);
  cellular_data_counter_update_cb(0LL, 0LL, 0, 0, 0, NULL);

  dc_unknown_bool_1 = TRUE;

  if (!connui_cell_datacounter_register(cellular_data_counter_update_cb,
                                        home_counter, NULL))
  {
    CONNUI_ERR("Unable to register data counter update callback");
  }

  gtk_widget_show_all(data_counters_dialog);

  return GTK_DIALOG(data_counters_dialog);
}
