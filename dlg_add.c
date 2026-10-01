#define _GNU_SOURCE
#include "dlg.h"
#include <ctype.h>
#include <strings.h>

enum { K_DEV, K_PPD_REC, K_PPD_ALL, K_ADD };

typedef struct _Add Add;
struct _Add
{
   Evas_Object *win, *dev_list, *ppd_list;
   Evas_Object *e_uri, *e_filter, *e_name, *e_info, *e_loc, *ck_share;
   Evas_Object *status, *btn_add;
   Elm_Genlist_Item_Class *itc_dev, *itc_ppd;
   Eina_List *devices, *ppds_rec, *ppds_all;   /* owned */
   Cp_Device *sel_dev;
   Cp_Ppd everywhere;                          /* pseudo driver entry */
   Eina_Bool all_loading, closed;
   int pending;                                /* running threads */
   void (*done)(void);
};

typedef struct
{
   Add *d;
   int kind;
   Eina_List *list;    /* result of K_DEV / K_PPD_* */
   char *s[5];         /* K_ADD: name, uri, ppd, info, location; K_PPD_REC: [0]=device id */
   Eina_Bool shared, ok;
   char err[256];
} Job;

/* ------------------------------------------------------------------ */
/* Lifetime: the dialog struct outlives the window until all threads   */
/* have reported back.                                                 */

static void
_free_add(Add *d)
{
   cp_devices_free(d->devices);
   cp_ppds_free(d->ppds_rec);
   cp_ppds_free(d->ppds_all);
   elm_genlist_item_class_free(d->itc_dev);   /* ref-counted */
   elm_genlist_item_class_free(d->itc_ppd);
   free(d);
}

static void
_maybe_free(Add *d)
{
   if (d->closed && d->pending <= 0) _free_add(d);
}

static void
_win_del(void *data, Evas *e EINA_UNUSED, Evas_Object *o EINA_UNUSED,
         void *ev EINA_UNUSED)
{
   Add *d = data;
   d->closed = EINA_TRUE;
   _maybe_free(d);
}

static void
_status(Add *d, const char *msg)
{
   dlg_label_set(d->status, msg);
}

/* ------------------------------------------------------------------ */
/* Threads                                                             */

static void
_job_discard(Job *j)
{
   int i;

   if (j->kind == K_DEV) cp_devices_free(j->list);
   else cp_ppds_free(j->list);
   for (i = 0; i < 5; i++) free(j->s[i]);
   free(j);
}

static void
_th_run(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;

   switch (j->kind)
     {
      case K_DEV:     j->list = cp_devices_get(); break;
      case K_PPD_REC: j->list = cp_ppds_get(j->s[0]); break;
      case K_PPD_ALL: j->list = cp_ppds_get(NULL); break;
      case K_ADD:
        j->ok = cp_printer_add(j->s[0], j->s[1], j->s[2], j->s[3], j->s[4],
                               j->shared);
        if (!j->ok) snprintf(j->err, sizeof(j->err), "%s", cp_last_error());
        break;
     }
}

static void _ppd_refill(Add *d);
static void _devs_fill(Add *d, Eina_List *list);

static void
_th_end(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   Add *d = j->d;

   if (!d->closed)
     {
        switch (j->kind)
          {
           case K_DEV:
             _devs_fill(d, j->list);
             j->list = NULL;
             _status(d, d->devices ?
                     "Select a device, or enter a URI manually." :
                     "No devices found. Enter a device URI manually.");
             break;

           case K_PPD_REC:
             /* ignore stale answers for a previously selected device */
             if (d->sel_dev && j->s[0] && !strcmp(d->sel_dev->device_id, j->s[0]))
               {
                  cp_ppds_free(d->ppds_rec);
                  d->ppds_rec = j->list;
                  j->list = NULL;
                  _ppd_refill(d);
               }
             break;

           case K_PPD_ALL:
             d->all_loading = EINA_FALSE;
             cp_ppds_free(d->ppds_all);
             d->ppds_all = j->list;
             j->list = NULL;
             _status(d, "");
             _ppd_refill(d);
             break;

           case K_ADD:
             if (j->ok)
               {
                  if (d->done) d->done();
                  evas_object_del(d->win);   /* sets d->closed */
               }
             else
               {
                  _status(d, j->err);
                  elm_object_disabled_set(d->btn_add, EINA_FALSE);
               }
             break;
          }
     }

   _job_discard(j);
   d->pending--;
   _maybe_free(d);
}

static void
_th_cancel(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   Add *d = j->d;

   _job_discard(j);
   d->pending--;
   _maybe_free(d);
}

static Job *
_job_new(Add *d, int kind)
{
   Job *j = calloc(1, sizeof(Job));
   j->d = d;
   j->kind = kind;
   d->pending++;
   return j;
}

static void
_job_start(Job *j)
{
   ecore_thread_run(_th_run, _th_end, _th_cancel, j);
}

static void
_launch(Add *d, int kind, const char *s0)
{
   Job *j = _job_new(d, kind);
   if (s0) j->s[0] = strdup(s0);
   _job_start(j);
}

/* ------------------------------------------------------------------ */
/* Devices                                                             */

static char *
_dev_text(void *data, Evas_Object *o EINA_UNUSED, const char *part)
{
   const Cp_Device *dv = data;

   if (!strcmp(part, "elm.text"))
     return strdup(*dv->make_model ? dv->make_model : dv->info);
   if (!strcmp(part, "elm.text.sub"))
     return strdup(dv->uri);
   return NULL;
}

static void
_suggest_name(const char *src, char *out, size_t len)
{
   size_t i = 0;
   Eina_Bool last_us = EINA_TRUE;

   for (; *src && i + 1 < len; src++)
     {
        if (isalnum((unsigned char)*src) || *src == '-')
          {
             out[i++] = *src;
             last_us = EINA_FALSE;
          }
        else if (!last_us)
          {
             out[i++] = '_';
             last_us = EINA_TRUE;
          }
     }
   while (i > 0 && out[i - 1] == '_') i--;
   out[i] = '\0';
}

static void
_cb_dev_sel(void *data, Evas_Object *o EINA_UNUSED, void *ev)
{
   Add *d = data;
   Cp_Device *dv = elm_object_item_data_get(ev);
   char name[128];

   if (!dv) return;
   d->sel_dev = dv;

   dlg_set_txt(d->e_uri, dv->uri);
   _suggest_name(*dv->make_model ? dv->make_model : dv->info,
                 name, sizeof(name));
   dlg_set_txt(d->e_name, name);
   dlg_set_txt(d->e_info, *dv->info ? dv->info : dv->make_model);

   cp_ppds_free(d->ppds_rec);
   d->ppds_rec = NULL;
   if (*dv->device_id) _launch(d, K_PPD_REC, dv->device_id);
   _ppd_refill(d);
}

static void
_devs_fill(Add *d, Eina_List *list)
{
   Eina_List *l;
   Cp_Device *dv;

   elm_genlist_clear(d->dev_list);
   d->sel_dev = NULL;
   cp_devices_free(d->devices);
   d->devices = list;

   EINA_LIST_FOREACH(list, l, dv)
     elm_genlist_item_append(d->dev_list, d->itc_dev, dv, NULL,
                             ELM_GENLIST_ITEM_NONE, _cb_dev_sel, d);
}

static void
_scan(Add *d)
{
   elm_genlist_clear(d->dev_list);
   d->sel_dev = NULL;
   _status(d, "Searching for printers (this can take ~15 s)...");
   _launch(d, K_DEV, NULL);
}

static void
_cb_rescan(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   _scan(data);
}

/* ------------------------------------------------------------------ */
/* Drivers                                                             */

static char *
_ppd_text(void *data, Evas_Object *o EINA_UNUSED, const char *part)
{
   const Cp_Ppd *p = data;

   return !strcmp(part, "elm.text") ? strdup(p->make_model) : NULL;
}

static Eina_Bool
_is_net(const char *uri)
{
   return !strncmp(uri, "dnssd:", 6) || !strncmp(uri, "ipp:", 4) ||
          !strncmp(uri, "ipps:", 5);
}

static void
_ppds_all_load(Add *d)
{
   if (d->all_loading) return;
   d->all_loading = EINA_TRUE;
   _status(d, "Loading driver list...");
   _launch(d, K_PPD_ALL, NULL);
}

static void
_ppd_refill(Add *d)
{
   Eina_List *src, *l;
   Elm_Object_Item *it_every, *first = NULL, *pick;
   Cp_Ppd *p;
   char *flt = dlg_txt(d->e_filter);
   char *uri = dlg_txt(d->e_uri);
   int n = 0;

   elm_genlist_clear(d->ppd_list);
   it_every = elm_genlist_item_append(d->ppd_list, d->itc_ppd, &d->everywhere,
                                      NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);

   if (*flt)
     {
        src = d->ppds_all;
        if (!src) _ppds_all_load(d);
     }
   else src = d->ppds_rec;

   EINA_LIST_FOREACH(src, l, p)
     {
        Elm_Object_Item *it;

        if (*flt && !strcasestr(p->make_model, flt)) continue;
        it = elm_genlist_item_append(d->ppd_list, d->itc_ppd, p, NULL,
                                     ELM_GENLIST_ITEM_NONE, NULL, NULL);
        if (!first) first = it;
        if (++n >= 400) break;
     }

   if (*flt) pick = first ? first : it_every;
   else pick = (_is_net(uri) || !first) ? it_every : first;
   elm_genlist_item_selected_set(pick, EINA_TRUE);

   free(flt);
   free(uri);
}

static void
_cb_filter(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   _ppd_refill(data);
}

/* ------------------------------------------------------------------ */
/* Add                                                                 */

static void
_cb_cancel(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   Add *d = data;
   evas_object_del(d->win);
}

static void
_cb_add(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   Add *d = data;
   Elm_Object_Item *it = elm_genlist_selected_item_get(d->ppd_list);
   Cp_Ppd *pp = it ? elm_object_item_data_get(it) : NULL;
   char *name = dlg_txt(d->e_name), *uri = dlg_txt(d->e_uri);
   char *info = dlg_txt(d->e_info), *loc = dlg_txt(d->e_loc);
   Job *j;

   if (!*name || !*uri || !pp)
     {
        _status(d, "Fill in the device URI and name, and choose a driver.");
        goto fail;
     }
   if (strpbrk(name, " \t/#"))
     {
        _status(d, "The name must not contain spaces, '/' or '#'.");
        goto fail;
     }

   j = _job_new(d, K_ADD);
   j->s[0] = name;
   j->s[1] = uri;
   j->s[2] = strdup(pp->name);
   j->s[3] = info;
   j->s[4] = loc;
   j->shared = elm_check_state_get(d->ck_share);

   elm_object_disabled_set(d->btn_add, EINA_TRUE);
   _status(d, "Adding printer (can take a few seconds)...");
   _job_start(j);
   return;

fail:
   free(name); free(uri); free(info); free(loc);
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */

static Evas_Object *
_vbox(Evas_Object *parent, Eina_Bool expand)
{
   Evas_Object *b = elm_box_add(parent);
   elm_box_padding_set(b, 0, 6);
   evas_object_size_hint_weight_set(b, EVAS_HINT_EXPAND,
                                    expand ? EVAS_HINT_EXPAND : 0.0);
   evas_object_size_hint_align_set(b, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(b);
   return b;
}

static Evas_Object *
_list(Evas_Object *parent, Evas_Object *box)
{
   Evas_Object *g = elm_genlist_add(parent);
   evas_object_size_hint_weight_set(g, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(g, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_min_set(g, 0, 130);
   elm_box_pack_end(box, g);
   evas_object_show(g);
   return g;
}

void
dlg_add_open(Evas_Object *parent, void (*done)(void))
{
   Add *d = calloc(1, sizeof(Add));
   Evas_Object *win, *bx, *fb, *hb, *tb, *b;

   d->done = done;
   d->everywhere.name = (char *)"everywhere";
   d->everywhere.make_model =
     (char *)"Driverless (IPP Everywhere) - recommended for network printers";

   d->itc_dev = elm_genlist_item_class_new();
   d->itc_dev->item_style = "double_label";
   d->itc_dev->func.text_get = _dev_text;
   d->itc_ppd = elm_genlist_item_class_new();
   d->itc_ppd->item_style = "default";
   d->itc_ppd->func.text_get = _ppd_text;

   win = d->win = elm_win_util_dialog_add(parent, "moksha-printers-add",
                                          "Add printer");
   elm_win_autodel_set(win, EINA_TRUE);
   evas_object_event_callback_add(win, EVAS_CALLBACK_DEL, _win_del, d);

   bx = _vbox(win, EINA_TRUE);
   elm_box_padding_set(bx, 0, 8);
   elm_win_resize_object_add(win, bx);

   /* 1. Device */
   fb = _vbox(win, EINA_TRUE);
   d->dev_list = _list(win, fb);
   hb = elm_box_add(win);
   elm_box_horizontal_set(hb, EINA_TRUE);
   evas_object_size_hint_weight_set(hb, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(hb, 0.0, 0.5);
   b = dlg_button(win, "Rescan", _cb_rescan, d);
   elm_box_pack_end(hb, b);
   evas_object_show(hb);
   elm_box_pack_end(fb, hb);
   tb = dlg_table(win);
   d->e_uri = dlg_entry(win);
   dlg_row(tb, 0, "Device URI", d->e_uri);
   elm_box_pack_end(fb, tb);
   elm_box_pack_end(bx, dlg_frame(win, "1. Device", fb, EINA_TRUE));

   /* 2. Driver */
   fb = _vbox(win, EINA_TRUE);
   d->e_filter = dlg_entry(win);
   elm_object_part_text_set(d->e_filter, "guide",
                            "Search all drivers, e.g. HP LaserJet");
   elm_box_pack_end(fb, d->e_filter);
   d->ppd_list = _list(win, fb);
   elm_box_pack_end(bx, dlg_frame(win, "2. Driver", fb, EINA_TRUE));

   /* 3. Details */
   tb = dlg_table(win);
   d->e_name = dlg_entry(win);
   dlg_row(tb, 0, "Name", d->e_name);
   d->e_info = dlg_entry(win);
   dlg_row(tb, 1, "Description", d->e_info);
   d->e_loc = dlg_entry(win);
   dlg_row(tb, 2, "Location", d->e_loc);
   d->ck_share = elm_check_add(win);
   elm_object_text_set(d->ck_share, "Share this printer");
   evas_object_size_hint_align_set(d->ck_share, 0.0, 0.5);
   dlg_row(tb, 3, NULL, d->ck_share);
   elm_box_pack_end(bx, dlg_frame(win, "3. Details", tb, EINA_FALSE));

   /* buttons */
   hb = elm_box_add(win);
   elm_box_horizontal_set(hb, EINA_TRUE);
   elm_box_padding_set(hb, 8, 0);
   evas_object_size_hint_weight_set(hb, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(hb, EVAS_HINT_FILL, 0.5);
   d->status = elm_label_add(win);
   evas_object_size_hint_weight_set(d->status, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(d->status, 0.0, 0.5);
   evas_object_show(d->status);
   elm_box_pack_end(hb, d->status);
   elm_box_pack_end(hb, dlg_button(win, "Cancel", _cb_cancel, d));
   d->btn_add = dlg_button(win, "Add printer", _cb_add, d);
   elm_box_pack_end(hb, d->btn_add);
   evas_object_show(hb);
   elm_box_pack_end(bx, hb);

   _ppd_refill(d);
   evas_object_smart_callback_add(d->e_filter, "changed", _cb_filter, d);
   _scan(d);

   evas_object_resize(win, 760, 700);
   evas_object_show(win);
}
