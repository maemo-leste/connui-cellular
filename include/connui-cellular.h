#ifndef __CONNUI_CELLULAR_H__
#define __CONNUI_CELLULAR_H__

#include <hildon/hildon.h>

#include "connui-cellular-sim.h"
#include "connui-cellular-net.h"
#include "connui-cellular-modem.h"
#include "connui-cellular-sups.h"
#include "connui-cellular-code-ui.h"

/* CALL */
void connui_cell_call_status_close(cell_call_status_cb cb);
gboolean connui_cell_call_status_register(cell_call_status_cb cb, gpointer user_data);

gboolean connui_cell_emergency_call();

/* datacounter */
void connui_cell_datacounter_close(cell_datacounter_cb cb);
void connui_cell_datacounter_reset();
gboolean connui_cell_datacounter_register(cell_datacounter_cb cb, gboolean home, gpointer user_data);
void connui_cell_datacounter_save(gboolean notification_enabled, const gchar *warning_limit);

#define CONNUI_ERROR (connui_error_quark())
GQuark connui_error_quark(void);

typedef enum connui_error {
    CONNUI_ERROR_INVALID_ARGS,          /* org.ofono.Error.InvalidArguments */
    CONNUI_ERROR_INVALID_FORMAT,        /* org.ofono.Error.InvalidFormat */
    CONNUI_ERROR_NOT_IMPLEMENTED,       /* org.ofono.Error.NotImplemented */
    CONNUI_ERROR_FAILED,                /* org.ofono.Error.Failed */
    CONNUI_ERROR_BUSY,                  /* org.ofono.Error.InProgress */
    CONNUI_ERROR_NOT_FOUND,             /* org.ofono.Error.NotFound */
    CONNUI_ERROR_NOT_ACTIVE,            /* org.ofono.Error.NotActive */
    CONNUI_ERROR_NOT_SUPPORTED,         /* org.ofono.Error.NotSupported */
    CONNUI_ERROR_NOT_AVAILABLE,         /* org.ofono.Error.NotAvailable */
    CONNUI_ERROR_TIMED_OUT,             /* org.ofono.Error.Timedout */
    CONNUI_ERROR_SIM_NOT_READY,         /* org.ofono.Error.SimNotReady */
    CONNUI_ERROR_IN_USE,                /* org.ofono.Error.InUse */
    CONNUI_ERROR_NOT_ATTACHED,          /* org.ofono.Error.NotAttached */
    CONNUI_ERROR_ATTACH_IN_PROGRESS,    /* org.ofono.Error.AttachInProgress */
    CONNUI_ERROR_NOT_REGISTERED,        /* org.ofono.Error.NotRegistered */
    CONNUI_ERROR_CANCELED,              /* org.ofono.Error.Canceled */
    CONNUI_ERROR_ACCESS_DENIED,         /* org.ofono.Error.AccessDenied */
    CONNUI_ERROR_EMERGENCY_ACTIVE,      /* org.ofono.Error.EmergencyActive */
    CONNUI_ERROR_INCORRECT_PASSWORD,    /* org.ofono.Error.IncorrectPassword */
    CONNUI_ERROR_NOT_ALLOWED,           /* org.ofono.Error.NotAllowed */
    CONNUI_ERROR_NOT_RECOGNIZED,        /* org.ofono.Error.NotRecognized */
    CONNUI_ERROR_NETWORK_TERMINATED,    /* org.ofono.Error.Terminated */
    CONNUI_NUM_ERRORS
} ConnuiError;

#endif /* __CONNUI_CELLULAR_H__ */
