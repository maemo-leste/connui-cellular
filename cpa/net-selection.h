#ifndef CELLULAR_SETTINGS_NETSELECTION_H
#define CELLULAR_SETTINGS_NETSELECTION_H

void cellular_net_selection_destroy();
void cellular_net_clear_cache();
void cellular_net_selection_reset_network();
gboolean cellular_net_selection_select(cell_network *net);
gboolean cellular_net_selection_select_automatic();
void cellular_net_selection_hide();

#endif // CELLULAR_SETTINGS_NETSELECTION_H
