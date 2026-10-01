#include <Elementary.h>
#include <stdio.h>
#include <string.h>
#include "cp_backend.h"
#include "dlg.h"

typedef struct
{
   Eina_List *list;
} Job;

static struct
{
   Evas_Object *win, *genlist, *status;
   Elm_Genlist_Item_Class *itc;
   Eina_List *printers;
   Ecore_Thread *th;
} app;

/* ---------- genlist ---------- */

static char *
_gl_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part)
{
   const Cp_Printer *p = data;
   char buf[512];

   if (!strcmp(part, "elm.text"))
     {
        snprintf(buf, sizeof(buf), "%s%s", p->name,
                 p->is_default ? "  (default)" : "");
        return strdup(buf);
     }
   if (!strcmp(part, "elm.text.sub"))
     {
        snprintf(buf, sizeof(buf), "%s%s%s%s%s",
                 cp_state_str(p->state),
                 p->accepting ? "" : ", not accepting jobs",
                 p->info && *p->info ? " - " : "",
                 p->info ? p->info : "",
                 p->reasons && *p->reasons ? " [" : "");
        if (p->reasons && *p->reasons)
          {
             strncat(buf, p->reasons, sizeof(buf) - strlen(buf) - 2);
             strncat(buf, "]", sizeof(buf) - strlen(buf) - 1);
          }
        return strdup(buf);
     }
   return NULL;
}

static Cp_Printer *
_selected(void)
{
   Elm_Object_Item *it = elm_genlist_selected_item_get(app.genlist);
   return it ? elm_object_item_data_get(it) : NULL;
}

static void
_status(const char *msg)
{
   elm_object_text_set(app.status, msg);
}

/* ---------- async refresh ---------- */

static void
_th_run(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   j->list = cp_printers_get();
}

static void
_th_end(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   Eina_List *l;
   Cp_Printer *p;

   elm_genlist_clear(app.genlist);
   cp_printers_free(app.printers);
   app.printers = j->list;

   EINA_LIST_FOREACH(app.printers, l, p)
     elm_genlist_item_append(app.genlist, app.itc, p, NULL,
                             ELM_GENLIST_ITEM_NONE, NULL, NULL);

   _status(app.printers ? "Ready" : "No printers (or CUPS not reachable)");
   free(j);
   app.th = NULL;
}

static void
_th_cancel(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   cp_printers_free(j->list);
   free(j);
   app.th = NULL;
}

static void
_refresh(void)
{
   if (app.th) return;
   _status("Loading...");
   app.th = ecore_thread_run(_th_run, _th_end, _th_cancel,
                             calloc(1, sizeof(Job)));
}

/* ---------- actions ----------
 * Synchronous for simplicity; localhost IPP is fast. Move them into
 * ecore_thread_run() if you want to support remote servers.
 * Auth: on 401/403 you will want cupsSetPasswordCB2() + an Elementary
 * password dialog here. */

typedef Eina_Bool (*Op)(const char *);

static void
_do(Op op)
{
   Cp_Printer *p = _selected();

   if (!p) { _status("Select a printer first"); return; }
   if (op(p->name)) _refresh();
   else _status(cp_last_error());
}

static void _cb_refresh(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED) { _refresh(); }
static void _cb_default(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED) { _do(cp_printer_set_default); }
static void _cb_clear(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED)   { _do(cp_jobs_cancel_all); }
static void _cb_remove(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED)  { _do(cp_printer_delete); }

static void
_cb_toggle(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED)
{
   Cp_Printer *p = _selected();

   if (!p) { _status("Select a printer first"); return; }
   _do(p->state == 5 ? cp_printer_resume : cp_printer_pause);
}

static void
_cb_add(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED)
{
   dlg_add_open(app.win, _refresh);
}

static void
_cb_props(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED)
{
   Cp_Printer *p = _selected();

   if (!p) { _status("Select a printer first"); return; }
   dlg_props_open(app.win, p->name, _refresh);
}

/* ---------- UI ---------- */

static Evas_Object *
_button(Evas_Object *box, const char *label, Evas_Smart_Cb cb)
{
   Evas_Object *b = elm_button_add(box);
   elm_object_text_set(b, label);
   evas_object_smart_callback_add(b, "clicked", cb, NULL);
   evas_object_show(b);
   elm_box_pack_end(box, b);
   return b;
}

static void
_win_del(void *d EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *e EINA_UNUSED)
{
   cp_printers_free(app.printers);
   app.printers = NULL;
   elm_genlist_item_class_free(app.itc);
   elm_exit();
}

EAPI_MAIN int
elm_main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   Evas_Object *bx, *tb;

   app.win = elm_win_util_standard_add("moksha-printers", "Printers");
   elm_win_autodel_set(app.win, EINA_TRUE);
   evas_object_smart_callback_add(app.win, "delete,request", _win_del, NULL);

   bx = elm_box_add(app.win);
   evas_object_size_hint_weight_set(bx, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(app.win, bx);
   evas_object_show(bx);

   tb = elm_box_add(bx);
   elm_box_horizontal_set(tb, EINA_TRUE);
   evas_object_size_hint_align_set(tb, 0.0, 0.0);
   elm_box_pack_end(bx, tb);
   evas_object_show(tb);
   _button(tb, "Add...", _cb_add);
   _button(tb, "Properties...", _cb_props);
   _button(tb, "Refresh", _cb_refresh);
   _button(tb, "Pause / Resume", _cb_toggle);
   _button(tb, "Set default", _cb_default);
   _button(tb, "Cancel jobs", _cb_clear);
   _button(tb, "Remove", _cb_remove);

   app.itc = elm_genlist_item_class_new();
   app.itc->item_style = "double_label";
   app.itc->func.text_get = _gl_text_get;

   app.genlist = elm_genlist_add(bx);
   evas_object_size_hint_weight_set(app.genlist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(app.genlist, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_smart_callback_add(app.genlist, "clicked,double", _cb_props, NULL);
   elm_box_pack_end(bx, app.genlist);
   evas_object_show(app.genlist);

   app.status = elm_label_add(bx);
   evas_object_size_hint_align_set(app.status, 0.0, 0.5);
   elm_box_pack_end(bx, app.status);
   evas_object_show(app.status);

   evas_object_resize(app.win, 640, 400);
   evas_object_show(app.win);

   _refresh();
   elm_run();
   return 0;
}
ELM_MAIN()
