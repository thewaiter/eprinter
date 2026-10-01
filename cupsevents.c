#include "cupsevents.h"
#include "notify.h"

#include <Ecore.h>
#include <Eldbus.h>
#include <stdio.h>
#include <string.h>

#define CUPS_PATH  "/org/cups/cupsd/Notifier"
#define CUPS_IFACE "org.cups.cupsd.Notifier"

/* ipp job-state values */
#define JSTATE_STOPPED   6
#define JSTATE_CANCELED  7
#define JSTATE_ABORTED   8
#define JSTATE_COMPLETED 9

/* Printer signals:  text, printer-uri, printer-name, printer-state,
 *                   printer-state-reasons, printer-is-accepting-jobs
 * Job signals add:  job-id, job-state, job-state-reasons, job-name,
 *                   job-impressions-completed                          */
#define SIG_PRINTER "sssusb"
#define SIG_JOB     "sssusbuussu"

static Eldbus_Connection *_conn;
static Eldbus_Signal_Handler *_sh;
static Ecore_Timer *_deb;
static void (*_changed)(void);

/* de-duplication: cupsd sends several signals per event */
static unsigned int _last_job, _last_jstate;
static char _last_pkey[256];

static Eina_Bool
_deb_fire(void *data EINA_UNUSED)
{
   _deb = NULL;
   if (_changed) _changed();
   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_is_problem(const char *r)
{
   if (!r || !*r) return EINA_FALSE;
   return strstr(r, "error") || strstr(r, "jam") || strstr(r, "door") ||
          strstr(r, "cover-open") || strstr(r, "empty") ||
          strstr(r, "offline") || strstr(r, "media-needed") ||
          strstr(r, "toner-low");
}

static void
_on_signal(void *data EINA_UNUSED, const Eldbus_Message *msg)
{
   const char *member = eldbus_message_member_get(msg);
   const char *sig = eldbus_message_signature_get(msg);
   const char *text, *uri, *pname, *reasons;
   unsigned int pstate;
   Eina_Bool accepting;

   if (sig && !strcmp(sig, SIG_JOB))
     {
        const char *jreasons, *jname;
        unsigned int jid, jstate, impr;

        if (eldbus_message_arguments_get(msg, SIG_JOB, &text, &uri, &pname,
                                         &pstate, &reasons, &accepting,
                                         &jid, &jstate, &jreasons, &jname,
                                         &impr) &&
            jstate >= JSTATE_STOPPED &&
            (jid != _last_job || jstate != _last_jstate))
          {
             char body[512];

             _last_job = jid;
             _last_jstate = jstate;
             snprintf(body, sizeof(body), "%s (%s)",
                      (jname && *jname) ? jname : "Job", pname);

             if (jstate == JSTATE_COMPLETED)
               notify_send("Printing finished", body, "printer");
             else if (jstate == JSTATE_ABORTED)
               notify_send("Printing failed", body, "printer-error");
             else if (jstate == JSTATE_STOPPED)
               notify_send("Print job stopped", body, "printer-error");
             /* JSTATE_CANCELED: the user did it, stay quiet */
          }
     }
   else if (sig && !strcmp(sig, SIG_PRINTER))
     {
        if (eldbus_message_arguments_get(msg, SIG_PRINTER, &text, &uri,
                                         &pname, &pstate, &reasons,
                                         &accepting))
          {
             Eina_Bool stopped = member && !strcmp(member, "PrinterStopped");

             if (stopped || _is_problem(reasons))
               {
                  char key[256], body[512];

                  snprintf(key, sizeof(key), "%s|%s", pname, reasons);
                  if (strcmp(key, _last_pkey))
                    {
                       snprintf(_last_pkey, sizeof(_last_pkey), "%s", key);
                       snprintf(body, sizeof(body), "%s: %s", pname,
                                (reasons && *reasons && strcmp(reasons, "none"))
                                ? reasons : (text && *text ? text : "stopped"));
                       notify_send("Printer needs attention", body,
                                   "printer-error");
                    }
               }
             else _last_pkey[0] = '\0';   /* problem gone: allow re-notify */
          }
     }

   /* any signal: re-read the printer list (debounced) */
   if (!_deb) _deb = ecore_timer_add(0.6, _deb_fire, NULL);
}

Eina_Bool
cups_events_init(void (*changed)(void))
{
   _changed = changed;
   _conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   if (!_conn) return EINA_FALSE;

   /* no sender / member filter: cupsd's unique name is not known in advance */
   _sh = eldbus_signal_handler_add(_conn, NULL, CUPS_PATH, CUPS_IFACE, NULL,
                                   _on_signal, NULL);
   return _sh ? EINA_TRUE : EINA_FALSE;
}

void
cups_events_shutdown(void)
{
   if (_deb) { ecore_timer_del(_deb); _deb = NULL; }
   if (_sh) eldbus_signal_handler_del(_sh);
   if (_conn) eldbus_connection_unref(_conn);
   _sh = NULL;
   _conn = NULL;
}
