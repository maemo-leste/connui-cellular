#ifndef __CONNUI_CELL_OFONO_CONTEXT_H__
#define __CONNUI_CELL_OFONO_CONTEXT_H__

#include "connui-cellular.h"
#include "context.h"


/* TODO: maybe have include/ofono-context.c */

/* TODO: return types of many of these void's */
gboolean register_ofono(connui_cell_context *ctx);
void unregister_ofono(connui_cell_context *ctx);

/* TODO: could use connui_cell_context_get(), but meh */
void set_sim(connui_cell_context *ctx);
void release_sim(connui_cell_context *ctx);

/* TODO: could use connui_cell_context_get(), but meh */
void set_netreg(connui_cell_context *ctx);
void release_netreg(connui_cell_context *ctx);

void ofono_wait_ready(connui_cell_context *ctx);

#endif /* __CONNUI_CELL_OFONO_CONTEXT_H__ */
