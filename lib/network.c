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

