#include "notify.h"

#include <Eldbus.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFY_BUS  "org.freedesktop.Notifications"
#define NOTIFY_PATH "/org/freedesktop/Notifications"
#define TIMEOUT_MS  8000

static Eldbus_Connection *_conn;
static Eldbus_Object *_obj;
static Eldbus_Proxy *_proxy;

void
notify_init(void)
{
   _conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SESSION);
   if (!_conn) return;
   _obj = eldbus_object_get(_conn, NOTIFY_BUS, NOTIFY_PATH);
   _proxy = eldbus_proxy_get(_obj, NOTIFY_BUS);
}

void
notify_shutdown(void)
{
   if (_proxy) eldbus_proxy_unref(_proxy);
   if (_obj) eldbus_object_unref(_obj);
   if (_conn) eldbus_connection_unref(_conn);
   _proxy = NULL; _obj = NULL; _conn = NULL;
}

/* body-markup capability: escape & < > */
static char *
_escape(const char *s)
{
   size_t n = 1;
   const char *p;
   char *out, *o;

   if (!s) return strdup("");
   for (p = s; *p; p++)
     n += (*p == '&') ? 5 : (*p == '<' || *p == '>') ? 4 : 1;
   o = out = malloc(n);
   for (p = s; *p; p++)
     {
        if (*p == '&') { memcpy(o, "&amp;", 5); o += 5; }
        else if (*p == '<') { memcpy(o, "&lt;", 4); o += 4; }
        else if (*p == '>') { memcpy(o, "&gt;", 4); o += 4; }
        else *o++ = *p;
     }
   *o = '\0';
   return out;
}

void
notify_send(const char *summary, const char *body, const char *icon)
{
   Eldbus_Message *msg;
   Eldbus_Message_Iter *it, *arr;
   char *b;

   if (!_proxy) return;

   msg = eldbus_proxy_method_call_new(_proxy, "Notify");
   it = eldbus_message_iter_get(msg);

   /* app_name, replaces_id, app_icon, summary, body */
   b = _escape(body);
   eldbus_message_iter_arguments_append(it, "susss", "Printers", 0,
                                        icon ? icon : "printer",
                                        summary ? summary : "", b);
   free(b);

   /* actions: as (empty) */
   arr = eldbus_message_iter_container_new(it, 'a', "s");
   eldbus_message_iter_container_close(it, arr);
   /* hints: a{sv} (empty) */
   arr = eldbus_message_iter_container_new(it, 'a', "{sv}");
   eldbus_message_iter_container_close(it, arr);
   /* expire_timeout */
   eldbus_message_iter_basic_append(it, 'i', TIMEOUT_MS);

   eldbus_proxy_send(_proxy, msg, NULL, NULL, -1);
}
