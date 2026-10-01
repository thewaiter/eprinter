#ifndef CUPSEVENTS_H
#define CUPSEVENTS_H

#include <Eina.h>

/* Listens to cupsd's D-Bus signals (org.cups.cupsd.Notifier on the system bus).
 * - shows desktop notifications for finished/failed jobs and printer problems
 * - calls changed() (debounced, 0.6 s) whenever anything happened, so the
 *   caller can re-read the printer list. */
Eina_Bool cups_events_init(void (*changed)(void));
void      cups_events_shutdown(void);

#endif
