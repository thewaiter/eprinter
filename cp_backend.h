#ifndef CP_BACKEND_H
#define CP_BACKEND_H

#include <Eina.h>

typedef struct
{
   char *name;
   char *info;         /* description */
   char *location;
   char *make_model;
   char *uri;          /* device-uri */
   char *reasons;      /* printer-state-reasons, joined by ", " */
   int state;          /* ipp_pstate_t: 3 idle, 4 processing, 5 stopped */
   Eina_Bool accepting;
   Eina_Bool shared;
   Eina_Bool is_default;
} Cp_Printer;

typedef struct
{
   char *uri;
   char *info;
   char *make_model;
   char *device_id;    /* IEEE 1284 id, may be empty */
} Cp_Device;

typedef struct
{
   char *name;         /* ppd-name, or "everywhere" */
   char *make_model;
} Cp_Ppd;

typedef struct
{
   char *key;          /* PPD keyword, e.g. "PageSize" */
   char *val;          /* choice, e.g. "A4" */
} Cp_Opt;

/* ---- All calls below are blocking: run them from a thread. ---- */

/* Printers */
Eina_List  *cp_printers_get(void);
Cp_Printer *cp_printer_get(const char *name);
void        cp_printer_free(Cp_Printer *p);
void        cp_printers_free(Eina_List *l);
const char *cp_state_str(int state);

/* Discovery (cupsGetDevices; may take ~15 s) */
Eina_List  *cp_devices_get(void);
void        cp_devices_free(Eina_List *l);

/* Drivers. device_id != NULL: best matches for that device (max 50, ranked).
 * NULL: the whole English list, sorted by make-and-model. */
Eina_List  *cp_ppds_get(const char *device_id);
void        cp_ppds_free(Eina_List *l);

/* Add / modify. ppd_name may be "everywhere" for driverless. */
Eina_Bool   cp_printer_add(const char *name, const char *device_uri,
                           const char *ppd_name, const char *info,
                           const char *location, Eina_Bool shared);
/* NULL string = leave unchanged, shared < 0 = leave unchanged.
 * opts: list of Cp_Opt, written as "<key>-default". */
Eina_Bool   cp_printer_modify(const char *name, const char *device_uri,
                              const char *info, const char *location,
                              int shared, Eina_List *opts);

/* Properties: PPD of the queue (temp file path, caller unlink()s and free()s)
 * and the queue's "<key>-default" values (key -> char *, free with eina_hash_free). */
char       *cp_printer_ppd_fetch(const char *name);
Eina_Hash  *cp_printer_defaults_get(const char *name);

/* Simple operations */
Eina_Bool   cp_printer_pause(const char *name);
Eina_Bool   cp_printer_resume(const char *name);
Eina_Bool   cp_printer_set_default(const char *name);
Eina_Bool   cp_printer_delete(const char *name);
Eina_Bool   cp_jobs_cancel_all(const char *name);

/* Last CUPS error of the *calling thread*. Copy it inside the worker thread. */
const char *cp_last_error(void);

#endif
