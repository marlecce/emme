#ifndef LOGGING_COMMON_H
#define LOGGING_COMMON_H

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

typedef enum {
    LOG_FORMAT_PLAIN, // formato testuale
    LOG_FORMAT_JSON   // formato JSON
} LogFormat;

typedef struct {
    char file[256];       // Nome del file di log
    LogLevel level;       // Livello minimo per i messaggi
    LogFormat format;     // Formato di output
    size_t buffer_size;   // Dimensione del ring buffer per i log

    /* Parametri per il rollover */
    size_t rollover_size; // Dimensione massima del file prima del rollover (0 per disabilitare)
    int rollover_daily;   // 1 se il rollover giornaliero è abilitato, 0 altrimenti

    /* Appender abilitati */
    int appender_flags;   // Bitmask: es. APPENDER_FILE, APPENDER_CONSOLE
} LoggingConfig;

#endif
