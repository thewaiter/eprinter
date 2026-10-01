#ifndef NOTIFY_H
#define NOTIFY_H

/* Desktop notifications via org.freedesktop.Notifications (session bus).
 * Moksha's "Notification" module implements this service. */

void notify_init(void);
void notify_shutdown(void);
/* icon: freedesktop icon name (e.g. "printer", "printer-error") or NULL.
 * body is plain text (escaped internally). */
void notify_send(const char *summary, const char *body, const char *icon);

#endif
