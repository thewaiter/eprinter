#include "cp_backend.h"

#include <cups/cups.h>
#include <cups/adminutil.h>   /* cupsGetDevices */
#include <cups/ppd.h>         /* cupsGetPPD3 */
#include <cups/http.h>
#include <cups/ipp.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

static const char *_pattrs[] =
{
   "printer-name", "printer-info", "printer-location",
   "printer-make-and-model", "device-uri", "printer-state",
   "printer-state-reasons", "printer-is-accepting-jobs", "printer-is-shared"
};
#define N_PATTRS ((int)(sizeof(_pattrs) / sizeof(_pattrs[0])))

static char *
_dup(const char *s)
{
   return strdup(s ? s : "");
}

/* ------------------------------------------------------------------ */
/* Request helpers                                                     */

static void
_uri_for(const char *name, char *buf, size_t len)
{
   httpAssembleURIf(HTTP_URI_CODING_ALL, buf, (int)len, "ipp", NULL,
                    "localhost", 0, "/printers/%s", name);
}

static ipp_t *
_new_req(ipp_op_t op, const char *name)
{
   char uri[HTTP_MAX_URI];
   ipp_t *req = ippNewRequest(op);

   if (name)
     {
        _uri_for(name, uri, sizeof(uri));
        ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_URI,
                     "printer-uri", NULL, uri);
     }
   ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_NAME,
                "requesting-user-name", NULL, cupsUser());
   return req;
}

static Eina_Bool
_run(ipp_t *req, const char *resource)
{
   ipp_t *resp = cupsDoRequest(CUPS_HTTP_DEFAULT, req, resource);

   if (resp) ippDelete(resp);
   return cupsLastError() < IPP_STATUS_REDIRECTION_OTHER_SITE;
}

static Eina_Bool
_simple_op(ipp_op_t op, const char *name)
{
   return _run(_new_req(op, name), "/admin/");
}

const char *
cp_last_error(void)
{
   return cupsLastErrorString();
}

Eina_Bool cp_printer_pause(const char *n)       { return _simple_op(IPP_OP_PAUSE_PRINTER, n); }
Eina_Bool cp_printer_resume(const char *n)      { return _simple_op(IPP_OP_RESUME_PRINTER, n); }
Eina_Bool cp_printer_set_default(const char *n) { return _simple_op(IPP_OP_CUPS_SET_DEFAULT, n); }
Eina_Bool cp_printer_delete(const char *n)      { return _simple_op(IPP_OP_CUPS_DELETE_PRINTER, n); }
Eina_Bool cp_jobs_cancel_all(const char *n)     { return _simple_op(IPP_OP_CANCEL_JOBS, n); }

const char *
cp_state_str(int state)
{
   switch (state)
     {
      case IPP_PSTATE_IDLE:       return "Idle";
      case IPP_PSTATE_PROCESSING: return "Printing";
      case IPP_PSTATE_STOPPED:    return "Paused";
      default:                    return "Unknown";
     }
}

/* ------------------------------------------------------------------ */
/* Printers                                                            */

static char *
_join_keywords(ipp_attribute_t *attr)
{
   Eina_Strbuf *sb = eina_strbuf_new();
   int i, n = ippGetCount(attr);
   char *ret;

   for (i = 0; i < n; i++)
     {
        const char *s = ippGetString(attr, i, NULL);
        if (!s || !strcmp(s, "none")) continue;
        if (eina_strbuf_length_get(sb)) eina_strbuf_append(sb, ", ");
        eina_strbuf_append(sb, s);
     }
   ret = eina_strbuf_string_steal(sb);
   eina_strbuf_free(sb);
   return ret;
}

static Eina_List *
_parse_printers(ipp_t *resp)
{
   Eina_List *out = NULL;
   const char *def = cupsGetDefault();
   char *defname = def ? strdup(def) : NULL;
   ipp_attribute_t *a = ippFirstAttribute(resp);

   while (a)
     {
        Cp_Printer *p;

        while (a && ippGetGroupTag(a) != IPP_TAG_PRINTER)
          a = ippNextAttribute(resp);
        if (!a) break;

        p = calloc(1, sizeof(Cp_Printer));
        p->accepting = EINA_TRUE;

        for (; a && ippGetGroupTag(a) == IPP_TAG_PRINTER;
             a = ippNextAttribute(resp))
          {
             const char *n = ippGetName(a);
             if (!n) continue;
             if (!strcmp(n, "printer-name") &&
                 ippGetValueTag(a) == IPP_TAG_NAME)
               p->name = _dup(ippGetString(a, 0, NULL));
             else if (!strcmp(n, "printer-info"))
               p->info = _dup(ippGetString(a, 0, NULL));
             else if (!strcmp(n, "printer-location"))
               p->location = _dup(ippGetString(a, 0, NULL));
             else if (!strcmp(n, "printer-make-and-model"))
               p->make_model = _dup(ippGetString(a, 0, NULL));
             else if (!strcmp(n, "device-uri"))
               p->uri = _dup(ippGetString(a, 0, NULL));
             else if (!strcmp(n, "printer-state"))
               p->state = ippGetInteger(a, 0);
             else if (!strcmp(n, "printer-state-reasons"))
               p->reasons = _join_keywords(a);
             else if (!strcmp(n, "printer-is-accepting-jobs"))
               p->accepting = ippGetBoolean(a, 0) ? EINA_TRUE : EINA_FALSE;
             else if (!strcmp(n, "printer-is-shared"))
               p->shared = ippGetBoolean(a, 0) ? EINA_TRUE : EINA_FALSE;
          }

        if (p->name)
          {
             p->is_default = defname && !strcmp(defname, p->name);
             out = eina_list_append(out, p);
          }
        else cp_printer_free(p);
     }

   free(defname);
   return out;
}

Eina_List *
cp_printers_get(void)
{
   ipp_t *req, *resp;
   Eina_List *out;

   req = ippNewRequest(IPP_OP_CUPS_GET_PRINTERS);
   ippAddStrings(req, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                 "requested-attributes", N_PATTRS, NULL, _pattrs);
   resp = cupsDoRequest(CUPS_HTTP_DEFAULT, req, "/");
   if (!resp) return NULL;
   out = _parse_printers(resp);
   ippDelete(resp);
   return out;
}

Cp_Printer *
cp_printer_get(const char *name)
{
   ipp_t *req, *resp;
   Eina_List *l;
   Cp_Printer *p;

   req = _new_req(IPP_OP_GET_PRINTER_ATTRIBUTES, name);
   ippAddStrings(req, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                 "requested-attributes", N_PATTRS, NULL, _pattrs);
   resp = cupsDoRequest(CUPS_HTTP_DEFAULT, req, "/");
   if (!resp) return NULL;
   l = _parse_printers(resp);
   ippDelete(resp);
   if (!l) return NULL;
   p = eina_list_data_get(l);
   l = eina_list_remove_list(l, l);
   cp_printers_free(l);
   return p;
}

void
cp_printer_free(Cp_Printer *p)
{
   if (!p) return;
   free(p->name); free(p->info); free(p->location); free(p->make_model);
   free(p->uri); free(p->reasons);
   free(p);
}

void
cp_printers_free(Eina_List *l)
{
   Cp_Printer *p;

   EINA_LIST_FREE(l, p) cp_printer_free(p);
}

/* ------------------------------------------------------------------ */
/* Device discovery                                                    */

static void
_dev_cb(const char *dclass EINA_UNUSED, const char *id, const char *info,
        const char *make_model, const char *uri,
        const char *loc EINA_UNUSED, void *data)
{
   Eina_List **l = data;
   Cp_Device *d;

   /* skip bare scheme entries such as "socket", "lpd", "ipp" */
   if (!uri || !strstr(uri, "://")) return;

   d = calloc(1, sizeof(Cp_Device));
   d->uri = _dup(uri);
   d->info = _dup(info);
   d->make_model = _dup(make_model);
   d->device_id = _dup(id);
   if (!strcasecmp(d->make_model, "unknown")) *d->make_model = '\0';
   *l = eina_list_append(*l, d);
}

Eina_List *
cp_devices_get(void)
{
   Eina_List *l = NULL;

   cupsGetDevices(CUPS_HTTP_DEFAULT, CUPS_TIMEOUT_DEFAULT, NULL, NULL,
                  _dev_cb, &l);
   return l;
}

void
cp_devices_free(Eina_List *l)
{
   Cp_Device *d;

   EINA_LIST_FREE(l, d)
     {
        free(d->uri); free(d->info); free(d->make_model); free(d->device_id);
        free(d);
     }
}

/* ------------------------------------------------------------------ */
/* Drivers (PPDs)                                                      */

static int
_ppd_cmp(const void *a, const void *b)
{
   const Cp_Ppd *x = a, *y = b;
   return strcasecmp(x->make_model, y->make_model);
}

Eina_List *
cp_ppds_get(const char *device_id)
{
   static const char *want[] = { "ppd-name", "ppd-make-and-model" };
   Eina_List *out = NULL;
   ipp_t *req, *resp;
   ipp_attribute_t *a;
   Eina_Bool ranked = (device_id && *device_id);

   req = ippNewRequest(IPP_OP_CUPS_GET_PPDS);
   ippAddStrings(req, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                 "requested-attributes", 2, NULL, want);
   ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
                "ppd-natural-language", NULL, "en");
   if (ranked)
     {
        ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_TEXT,
                     "ppd-device-id", NULL, device_id);
        ippAddInteger(req, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "limit", 50);
     }

   resp = cupsDoRequest(CUPS_HTTP_DEFAULT, req, "/");
   if (!resp) return NULL;

   a = ippFirstAttribute(resp);
   while (a)
     {
        Cp_Ppd *p;

        while (a && ippGetGroupTag(a) != IPP_TAG_PRINTER)
          a = ippNextAttribute(resp);
        if (!a) break;

        p = calloc(1, sizeof(Cp_Ppd));
        for (; a && ippGetGroupTag(a) == IPP_TAG_PRINTER;
             a = ippNextAttribute(resp))
          {
             const char *n = ippGetName(a);
             if (!n) continue;
             if (!strcmp(n, "ppd-name"))
               p->name = _dup(ippGetString(a, 0, NULL));
             else if (!strcmp(n, "ppd-make-and-model"))
               p->make_model = _dup(ippGetString(a, 0, NULL));
          }

        if (p->name && p->make_model)
          out = eina_list_append(out, p);
        else
          {
             free(p->name); free(p->make_model); free(p);
          }
     }
   ippDelete(resp);

   if (!ranked) out = eina_list_sort(out, 0, _ppd_cmp);
   return out;
}

void
cp_ppds_free(Eina_List *l)
{
   Cp_Ppd *p;

   EINA_LIST_FREE(l, p)
     {
        free(p->name); free(p->make_model); free(p);
     }
}

/* ------------------------------------------------------------------ */
/* Add / modify                                                        */

Eina_Bool
cp_printer_add(const char *name, const char *device_uri, const char *ppd_name,
               const char *info, const char *location, Eina_Bool shared)
{
   ipp_t *req = _new_req(IPP_OP_CUPS_ADD_MODIFY_PRINTER, name);

   ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_URI,
                "device-uri", NULL, device_uri);
   if (ppd_name && *ppd_name)
     ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_NAME,
                  "ppd-name", NULL, ppd_name);
   if (info && *info)
     ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_TEXT,
                  "printer-info", NULL, info);
   if (location && *location)
     ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_TEXT,
                  "printer-location", NULL, location);
   ippAddBoolean(req, IPP_TAG_PRINTER, "printer-is-shared", shared ? 1 : 0);
   /* enable the new queue */
   ippAddBoolean(req, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);
   ippAddInteger(req, IPP_TAG_PRINTER, IPP_TAG_ENUM,
                 "printer-state", IPP_PSTATE_IDLE);
   return _run(req, "/admin/");
}

Eina_Bool
cp_printer_modify(const char *name, const char *device_uri, const char *info,
                  const char *location, int shared, Eina_List *opts)
{
   ipp_t *req = _new_req(IPP_OP_CUPS_ADD_MODIFY_PRINTER, name);
   Eina_List *l;
   Cp_Opt *o;

   if (device_uri && *device_uri)
     ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_URI,
                  "device-uri", NULL, device_uri);
   if (info)
     ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_TEXT,
                  "printer-info", NULL, info);
   if (location)
     ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_TEXT,
                  "printer-location", NULL, location);
   if (shared >= 0)
     ippAddBoolean(req, IPP_TAG_PRINTER, "printer-is-shared", shared ? 1 : 0);

   EINA_LIST_FOREACH(opts, l, o)
     {
        char key[256];
        snprintf(key, sizeof(key), "%s-default", o->key);
        ippAddString(req, IPP_TAG_PRINTER, IPP_TAG_NAME, key, NULL, o->val);
     }
   return _run(req, "/admin/");
}

/* ------------------------------------------------------------------ */
/* Properties data                                                     */

char *
cp_printer_ppd_fetch(const char *name)
{
   char buf[1024] = "";
   time_t mod = 0;
   http_status_t st;

   st = cupsGetPPD3(CUPS_HTTP_DEFAULT, name, &mod, buf, sizeof(buf));
   if (st != HTTP_STATUS_OK)
     {
        if (*buf) unlink(buf);
        return NULL;
     }
   return strdup(buf);
}

Eina_Hash *
cp_printer_defaults_get(const char *name)
{
   ipp_t *req, *resp;
   ipp_attribute_t *a;
   Eina_Hash *h;

   req = _new_req(IPP_OP_GET_PRINTER_ATTRIBUTES, name);
   resp = cupsDoRequest(CUPS_HTTP_DEFAULT, req, "/");
   if (!resp) return NULL;

   h = eina_hash_string_superfast_new(free);
   for (a = ippFirstAttribute(resp); a; a = ippNextAttribute(resp))
     {
        const char *n = ippGetName(a);
        ipp_tag_t t;
        size_t len;
        char key[256];

        if (!n) continue;
        len = strlen(n);
        if (len <= 8 || len - 8 >= sizeof(key)) continue;
        if (strcmp(n + len - 8, "-default")) continue;
        t = ippGetValueTag(a);
        if (t != IPP_TAG_KEYWORD && t != IPP_TAG_NAME && t != IPP_TAG_TEXT)
          continue;
        memcpy(key, n, len - 8);
        key[len - 8] = '\0';
        eina_hash_set(h, key, _dup(ippGetString(a, 0, NULL)));
     }
   ippDelete(resp);
   return h;
}
