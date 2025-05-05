#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_LOG_ENTRY_SIZE 1024

/* Struttura per ciascun messaggio di log */
typedef struct {
    LogLevel level;
    char message[MAX_LOG_ENTRY_SIZE];
    struct timespec timestamp;
} LogEntry;

/* Variabili statiche per la gestione del ring buffer e del logging */
static LogEntry *log_buffer = NULL;
static atomic_size_t log_head;     // indice di scrittura
static atomic_size_t log_tail;     // indice di lettura
static size_t log_buffer_size = 0;

/* Variabili di configurazione */
static LoggingConfig current_config;
static FILE *log_fp = NULL;
static pthread_t logger_thread;
static atomic_int logger_running = ATOMIC_VAR_INIT(0);

/* Variabili per il rollover */
static struct timespec last_rollover;
static size_t current_file_size = 0;

/* Funzione di rollover: rinomina il file attuale e ne crea uno nuovo */
static void rollover_log_file(void) {
    if (!(current_config.appender_flags & APPENDER_FILE))
        return;

    // Chiudi il file corrente
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }

    // Costruisci il nome di backup con timestamp
    char backup_filename[512];
    char timebuf[64];
    struct tm tm_info;
    localtime_r(&last_rollover.tv_sec, &tm_info);
    strftime(timebuf, sizeof(timebuf), "%Y%m%d%H%M%S", &tm_info);
    snprintf(backup_filename, sizeof(backup_filename), "%s.%s.bak", current_config.file, timebuf);

    // Rinomina il file attuale in backup
    rename(current_config.file, backup_filename);

    // Riapri un nuovo file di log
    log_fp = fopen(current_config.file, "a");
    current_file_size = 0;
    clock_gettime(CLOCK_REALTIME, &last_rollover);
}

/* Verifica se occorre effettuare il rollover in base a dimensione o intervallo */
static void check_rollover(const char *formatted_message) {
    if (!(current_config.appender_flags & APPENDER_FILE))
        return;

    int do_rollover = 0;
    size_t msg_len = strlen(formatted_message);

    // Verifica la dimensione
    if (current_config.rollover_size > 0 && (current_file_size + msg_len) >= current_config.rollover_size) {
        do_rollover = 1;
    }
    // Verifica il rollover giornaliero
    if (current_config.rollover_daily) {
        time_t now_t;
        time(&now_t);
        struct tm now_tm, last_tm;
        localtime_r(&now_t, &now_tm);
        localtime_r(&last_rollover.tv_sec, &last_tm);
        // Se il giorno dell'anno è cambiato, effettua il rollover
        if (now_tm.tm_yday != last_tm.tm_yday || now_tm.tm_year != last_tm.tm_year) {
            do_rollover = 1;
        }
    }
    if (do_rollover) {
        rollover_log_file();
    }
}

/* Funzione del thread di logging: estrae dal buffer e scrive sugli appender */
static void *logger_thread_func(void *arg) {
    (void)arg;
    clock_gettime(CLOCK_REALTIME, &last_rollover);
    while (atomic_load(&logger_running) || (atomic_load(&log_head) != atomic_load(&log_tail))) {
        size_t tail = atomic_load(&log_tail);
        size_t head = atomic_load(&log_head);
        if (tail == head) {
            /* Buffer vuoto: attesa breve per evitare busy-wait */
            usleep(1000);
            continue;
        }
        LogEntry entry = log_buffer[tail % log_buffer_size];

        /* Formatta il timestamp */
        char timebuf[64];
        struct tm tm_info;
        localtime_r(&(entry.timestamp.tv_sec), &tm_info);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_info);

        /* Costruisce il messaggio formattato */
        char formatted_message[2048];
        if (current_config.format == LOG_FORMAT_PLAIN) {
            snprintf(formatted_message, sizeof(formatted_message),
                     "[%s.%03ld] [%d] %s\n",
                     timebuf, entry.timestamp.tv_nsec / 1000000, entry.level, entry.message);
        } else { // LOG_FORMAT_JSON
            snprintf(formatted_message, sizeof(formatted_message),
                     "{\"timestamp\": \"%s.%03ld\", \"level\": %d, \"message\": \"%s\"}\n",
                     timebuf, entry.timestamp.tv_nsec / 1000000, entry.level, entry.message);
        }

        /* Scrive sugli appender configurati */
        if (current_config.appender_flags & APPENDER_FILE) {
            check_rollover(formatted_message);
            if (log_fp) {
                fputs(formatted_message, log_fp);
                fflush(log_fp);
                current_file_size += strlen(formatted_message);
            }
        }
        if (current_config.appender_flags & APPENDER_CONSOLE) {
            fputs(formatted_message, stdout);
        }
        atomic_fetch_add(&log_tail, 1);
    }
    return NULL;
}

/* Inizializza il modulo di logging */
int log_init(const LoggingConfig *config) {

    // Initialize last_rollover with the current time
    if (clock_gettime(CLOCK_REALTIME, &last_rollover) != 0)
    {
        perror("Failed to initialize last_rollover");
        return -1;
    }

    if (!config || config->file[0] == '\0') return -1;
    current_config = *config;
   
    /* Alloca il buffer di log */
    log_buffer_size = (config->buffer_size > 0) ? config->buffer_size : 1024;
    log_buffer = malloc(sizeof(LogEntry) * log_buffer_size);
    if (!log_buffer)
        return -1;
    atomic_init(&log_head, 0);
    atomic_init(&log_tail, 0);

    /* Inizializza file di log se richiesto */
    if (current_config.appender_flags & APPENDER_FILE) {
        log_fp = fopen(current_config.file, "a");
        if (!log_fp) {
            free(log_buffer);
            return -1;
        }
        /* Determina la dimensione corrente del file */
        struct stat st;
        if (stat(current_config.file, &st) == 0) {
            current_file_size = st.st_size;
        } else {
            current_file_size = 0;
        }
    }

    atomic_store(&logger_running, 1);
    if (pthread_create(&logger_thread, NULL, logger_thread_func, NULL) != 0) {
        if (log_fp)
            fclose(log_fp);
        free(log_buffer);
        return -1;
    }
    return 0;
}

/* Funzione per inviare messaggi di log */
void log_message(LogLevel level, const char *format, ...) {
    if (level < current_config.level) return;
    
    size_t head = atomic_load(&log_head);
    size_t tail = atomic_load(&log_tail);
    /* Se il buffer è pieno, scarta il messaggio per non bloccare */
    if (head - tail >= log_buffer_size) {
        return;
    }
    
    LogEntry *entry = &log_buffer[head % log_buffer_size];
    entry->level = level;
    clock_gettime(CLOCK_REALTIME, &entry->timestamp);
    
    va_list args;
    va_start(args, format);
    vsnprintf(entry->message, MAX_LOG_ENTRY_SIZE, format, args);
    va_end(args);
    
    atomic_fetch_add(&log_head, 1);
}

/* Chiude il modulo di logging e libera le risorse */
void log_shutdown(void) {
    atomic_store(&logger_running, 0);
    pthread_join(logger_thread, NULL);
    if (log_buffer) {
        free(log_buffer);
        log_buffer = NULL;
    }
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}
