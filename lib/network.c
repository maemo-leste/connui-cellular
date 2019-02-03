#include <glib.h>
#include <connui/connui-log.h>

#include "connui-cellular.h"

void
connui_cell_network_free(cell_network *network)
{
  if (network)
  {
    g_free(network->country_code);
    g_free(network->operator_code);
    g_free(network->operator_name);
    g_free(network);
  }
  else
    CONNUI_ERR("network == NULL");
}

cell_network *
connui_cell_network_dup(const cell_network *network)
{
  cell_network *network_dup;

  if (!network)
    return NULL;

  network_dup = g_new0(cell_network, 1);

  *network_dup = *network;
  network_dup->operator_code = g_strdup(network->operator_code);
  network_dup->country_code = g_strdup(network->country_code);
  network_dup->operator_name = g_strdup(network->operator_name);

  return network_dup;
}
