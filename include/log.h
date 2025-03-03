#ifndef LOG_H
#define LOG_H

#include <stddef.h>
#include <stdint.h>
#include "logging_common.h"

/* Bitmask per appender */
#define APPENDER_FILE    0x01
#define APPENDER_CONSOLE 0x02

/* Inizializza il logger con la configurazione specificata */
int log_init(const LoggingConfig *config);

/* Invia un messaggio di log; il formato è come printf */
void log_message(LogLevel level, const char *format, ...);

/* Termina il logger e libera le risorse */
void log_shutdown(void);

#endif // LOG_H
