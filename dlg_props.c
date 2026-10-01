#include "dlg.h"
#include <cups/ppd.h>
#include <unistd.h>

enum { P_LOAD, P_SAVE };

typedef struct _Props Props;

typedef struct
{
   char *key;          /* PPD keyword */
   char *orig;         /* value when the dialog was opened */
   char *cur;          /* value currently selected */
   Evas_Object *hs;    /* hoversel showing the value */
} Opt;

typedef struct
{
   Opt *o;
   char *choice;       /* PPD choice keyword */
   char *text;         /* human-readable */
} Choice;

struct _Props
{
   Evas_Object *win, *e_info, *e_loc, *e_uri, *ck_share, *opt_box;
   Evas_Object *l_model, *status, *btn_save;
   char *name, *orig_uri;
   Eina_List *opts;      /* Opt*    */
   Eina_List *choices;   /* Choice* */
   Eina_Bool closed;
   int pending;
   void (*done)(void);
};

typedef struct
{
   Props *p;
   int kind;
   char *name;
   /* P_LOAD result */
   Cp_Printer *pr;
   char *ppd_file;
   Eina_Hash *defs;
   /* P_SAVE input */
   char *s[3];           /* uri (or NULL), info, location */
   int shared;
   Eina_List *opts;      /* Cp_Opt* */
   Eina_Bool ok;
   char err[256];
} Job;

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */

static void
_free_props(Props *p)
{
   Opt *o;
   Choice *c;

   EINA_LIST_FREE(p->opts, o)
     {
        free(o->key); free(o->orig); free(o->cur); free(o);
     }
   EINA_LIST_FREE(p->choices, c)
     {
        free(c->choice); free(c->text); free(c);
     }
   free(p->name);
   free(p->orig_uri);
   free(p);
}

static void
_maybe_free(Props *p)
{
   if (p->closed && p->pending <= 0) _free_props(p);
}

static void
_win_del(void *data, Evas *e EINA_UNUSED, Evas_Object *o EINA_UNUSED,
         void *ev EINA_UNUSED)
{
   Props *p = data;
   p->closed = EINA_TRUE;
   _maybe_free(p);
}

static void
_status(Props *p, const char *msg)
{
   dlg_label_set(p->status, msg);
}

/* ------------------------------------------------------------------ */
/* PPD options UI                                                      */

static void
_cb_choice(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   Choice *c = data;

   free(c->o->cur);
   c->o->cur = strdup(c->choice);
   dlg_label_set(c->o->hs, c->text);
}

static void
_add_option(Props *p, Evas_Object *tb, int row, ppd_option_t *o,
            Eina_Hash *defs)
{
   Opt *op = calloc(1, sizeof(Opt));
   const char *cur = o->defchoice;
   const char *ov = defs ? eina_hash_find(defs, o->keyword) : NULL;
   Evas_Object *hs;
   int i;

   if (ov && *ov) cur = ov;      /* queue-level default wins over PPD default */

   op->key = strdup(o->keyword);
   op->orig = strdup(cur);
   op->cur = strdup(cur);

   hs = op->hs = elm_hoversel_add(p->win);
   elm_hoversel_hover_parent_set(hs, p->win);
   evas_object_size_hint_weight_set(hs, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(hs, EVAS_HINT_FILL, 0.5);
   dlg_label_set(hs, cur);       /* fallback if no choice matches */

   for (i = 0; i < o->num_choices; i++)
     {
        Choice *c = calloc(1, sizeof(Choice));
        c->o = op;
        c->choice = strdup(o->choices[i].choice);
        c->text = strdup(o->choices[i].text);
        p->choices = eina_list_append(p->choices, c);
        elm_hoversel_item_add(hs, c->text, NULL, ELM_ICON_NONE,
                              _cb_choice, c);
        if (!strcmp(c->choice, cur)) dlg_label_set(hs, c->text);
     }

   p->opts = eina_list_append(p->opts, op);
   dlg_row(tb, row, *o->text ? o->text : o->keyword, hs);
}

static void
_add_group(Props *p, ppd_group_t *g, Eina_Hash *defs)
{
   Evas_Object *tb = dlg_table(p->win);
   int i, row = 0;

   for (i = 0; i < g->num_options; i++)
     {
        ppd_option_t *o = g->options + i;

        if (o->ui != PPD_UI_PICKONE && o->ui != PPD_UI_BOOLEAN) continue;
        if (!strcmp(o->keyword, "PageRegion")) continue;   /* mirrors PageSize */
        _add_option(p, tb, row++, o, defs);
     }

   if (row)
     elm_box_pack_end(p->opt_box, dlg_frame(p->win, g->text, tb, EINA_FALSE));
   else
     evas_object_del(tb);

   for (i = 0; i < g->num_subgroups; i++)
     _add_group(p, g->subgroups + i, defs);
}

static void
_note(Props *p, const char *msg)
{
   Evas_Object *l = elm_label_add(p->win);

   dlg_label_set(l, msg);
   elm_label_line_wrap_set(l, ELM_WRAP_MIXED);
   evas_object_size_hint_weight_set(l, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(l, 0.0, 0.0);
   evas_object_show(l);
   elm_box_pack_end(p->opt_box, l);
}

static void
_build_options(Props *p, const char *file, Eina_Hash *defs)
{
   ppd_file_t *ppd;
   int i;

   if (!file)
     {
        _note(p, "No PPD is available for this queue, so there are no "
                 "driver options to show.");
        return;
     }
   ppd = ppdOpenFile(file);
   if (!ppd)
     {
        _note(p, "The printer's PPD could not be parsed.");
        return;
     }
   ppdMarkDefaults(ppd);
   for (i = 0; i < ppd->num_groups; i++)
     _add_group(p, ppd->groups + i, defs);
   if (!p->opts) _note(p, "This printer has no configurable options.");
   ppdClose(ppd);
}

/* ------------------------------------------------------------------ */
/* Threads                                                             */

static void
_job_discard(Job *j)
{
   Cp_Opt *o;
   int i;

   cp_printer_free(j->pr);
   if (j->ppd_file)
     {
        unlink(j->ppd_file);
        free(j->ppd_file);
     }
   if (j->defs) eina_hash_free(j->defs);
   EINA_LIST_FREE(j->opts, o)
     {
        free(o->key); free(o->val); free(o);
     }
   for (i = 0; i < 3; i++) free(j->s[i]);
   free(j->name);
   free(j);
}

static void
_th_run(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;

   if (j->kind == P_LOAD)
     {
        j->pr = cp_printer_get(j->name);
        j->ppd_file = cp_printer_ppd_fetch(j->name);
        j->defs = cp_printer_defaults_get(j->name);
     }
   else
     {
        j->ok = cp_printer_modify(j->name, j->s[0], j->s[1], j->s[2],
                                  j->shared, j->opts);
        if (!j->ok) snprintf(j->err, sizeof(j->err), "%s", cp_last_error());
     }
}

static void
_loaded(Props *p, Job *j)
{
   Cp_Printer *pr = j->pr;

   if (pr)
     {
        dlg_label_set(p->l_model, *pr->make_model ? pr->make_model : "-");
        dlg_set_txt(p->e_info, pr->info);
        dlg_set_txt(p->e_loc, pr->location);
        dlg_set_txt(p->e_uri, pr->uri);
        elm_check_state_set(p->ck_share, pr->shared);
        p->orig_uri = strdup(pr->uri);
        elm_object_disabled_set(p->btn_save, EINA_FALSE);
        _status(p, "");
     }
   else
     _status(p, "Could not read the printer's attributes.");

   _build_options(p, j->ppd_file, j->defs);
}

static void
_th_end(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   Props *p = j->p;

   if (!p->closed)
     {
        if (j->kind == P_LOAD) _loaded(p, j);
        else if (j->ok)
          {
             if (p->done) p->done();
             evas_object_del(p->win);   /* sets p->closed */
          }
        else
          {
             _status(p, j->err);
             elm_object_disabled_set(p->btn_save, EINA_FALSE);
          }
     }

   _job_discard(j);
   p->pending--;
   _maybe_free(p);
}

static void
_th_cancel(void *data, Ecore_Thread *th EINA_UNUSED)
{
   Job *j = data;
   Props *p = j->p;

   _job_discard(j);
   p->pending--;
   _maybe_free(p);
}

static Job *
_job_start(Props *p, int kind)
{
   Job *j = calloc(1, sizeof(Job));

   j->p = p;
   j->kind = kind;
   j->name = strdup(p->name);
   p->pending++;
   return j;
}

/* ------------------------------------------------------------------ */
/* Save / cancel                                                       */

static void
_cb_cancel(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   Props *p = data;
   evas_object_del(p->win);
}

static void
_cb_save(void *data, Evas_Object *o EINA_UNUSED, void *ev EINA_UNUSED)
{
   Props *p = data;
   Job *j = _job_start(p, P_SAVE);
   Eina_List *l;
   Opt *op;
   char *uri = dlg_txt(p->e_uri);

   /* only send the URI if it actually changed */
   if (*uri && (!p->orig_uri || strcmp(uri, p->orig_uri))) j->s[0] = uri;
   else free(uri);
   j->s[1] = dlg_txt(p->e_info);
   j->s[2] = dlg_txt(p->e_loc);
   j->shared = elm_check_state_get(p->ck_share) ? 1 : 0;

   EINA_LIST_FOREACH(p->opts, l, op)
     {
        Cp_Opt *co;

        if (!strcmp(op->cur, op->orig)) continue;
        co = calloc(1, sizeof(Cp_Opt));
        co->key = strdup(op->key);
        co->val = strdup(op->cur);
        j->opts = eina_list_append(j->opts, co);
     }

   elm_object_disabled_set(p->btn_save, EINA_TRUE);
   _status(p, "Saving...");
   ecore_thread_run(_th_run, _th_end, _th_cancel, j);
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */

void
dlg_props_open(Evas_Object *parent, const char *name, void (*done)(void))
{
   Props *p = calloc(1, sizeof(Props));
   Evas_Object *win, *bx, *tb, *sc, *hb, *l;
   char title[256];
   Job *j;

   p->name = strdup(name);
   p->done = done;

   snprintf(title, sizeof(title), "Printer properties - %s", name);
   win = p->win = elm_win_util_dialog_add(parent, "moksha-printers-props", title);
   elm_win_autodel_set(win, EINA_TRUE);
   evas_object_event_callback_add(win, EVAS_CALLBACK_DEL, _win_del, p);

   bx = elm_box_add(win);
   elm_box_padding_set(bx, 0, 8);
   evas_object_size_hint_weight_set(bx, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, bx);
   evas_object_show(bx);

   /* general */
   tb = dlg_table(win);
   l = elm_label_add(win);
   dlg_label_set(l, name);
   evas_object_size_hint_align_set(l, 0.0, 0.5);
   dlg_row(tb, 0, "Name", l);
   p->l_model = elm_label_add(win);
   dlg_label_set(p->l_model, "...");
   evas_object_size_hint_align_set(p->l_model, 0.0, 0.5);
   dlg_row(tb, 1, "Model", p->l_model);
   p->e_info = dlg_entry(win);
   dlg_row(tb, 2, "Description", p->e_info);
   p->e_loc = dlg_entry(win);
   dlg_row(tb, 3, "Location", p->e_loc);
   p->e_uri = dlg_entry(win);
   dlg_row(tb, 4, "Device URI", p->e_uri);
   p->ck_share = elm_check_add(win);
   elm_object_text_set(p->ck_share, "Share this printer");
   evas_object_size_hint_align_set(p->ck_share, 0.0, 0.5);
   dlg_row(tb, 5, NULL, p->ck_share);
   elm_box_pack_end(bx, dlg_frame(win, "General", tb, EINA_FALSE));

   /* options (filled once the PPD is loaded) */
   p->opt_box = elm_box_add(win);
   elm_box_padding_set(p->opt_box, 0, 8);
   evas_object_size_hint_weight_set(p->opt_box, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(p->opt_box, EVAS_HINT_FILL, 0.0);
   evas_object_show(p->opt_box);

   sc = elm_scroller_add(win);
   elm_scroller_bounce_set(sc, EINA_FALSE, EINA_FALSE);
   elm_scroller_policy_set(sc, ELM_SCROLLER_POLICY_OFF, ELM_SCROLLER_POLICY_AUTO);
   evas_object_size_hint_weight_set(sc, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(sc, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_object_content_set(sc, p->opt_box);
   evas_object_show(sc);
   elm_box_pack_end(bx, dlg_frame(win, "Options", sc, EINA_TRUE));

   /* buttons */
   hb = elm_box_add(win);
   elm_box_horizontal_set(hb, EINA_TRUE);
   elm_box_padding_set(hb, 8, 0);
   evas_object_size_hint_weight_set(hb, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(hb, EVAS_HINT_FILL, 0.5);
   p->status = elm_label_add(win);
   dlg_label_set(p->status, "Loading...");
   evas_object_size_hint_weight_set(p->status, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(p->status, 0.0, 0.5);
   evas_object_show(p->status);
   elm_box_pack_end(hb, p->status);
   elm_box_pack_end(hb, dlg_button(win, "Cancel", _cb_cancel, p));
   p->btn_save = dlg_button(win, "Save", _cb_save, p);
   elm_object_disabled_set(p->btn_save, EINA_TRUE);   /* until loaded */
   elm_box_pack_end(hb, p->btn_save);
   evas_object_show(hb);
   elm_box_pack_end(bx, hb);

   j = _job_start(p, P_LOAD);
   ecore_thread_run(_th_run, _th_end, _th_cancel, j);

   evas_object_resize(win, 620, 720);
   evas_object_show(win);
}
