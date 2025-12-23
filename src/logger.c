#include "logger.h"
#include <stdlib.h>
#include <string.h>

static FILE *log_file = NULL;
static const char *level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

void logger_init(const char *log_path) {
    log_file = fopen(log_path, "a");
    if (log_file) {
        time_t now = time(NULL);
        char *timestamp = ctime(&now);
        timestamp[strlen(timestamp) - 1] = '\0';
        fprintf(log_file, "\n=== Tonarchy Installation Log - %s ===\n", timestamp);
        fflush(log_file);
    }
}

void logger_close(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void log_msg(LogLevel level, const char *fmt, ...) {
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(
        log_file,
        "[%02d:%02d:%02d] [%s] ",
        t->tm_hour,
        t->tm_min,
        t->tm_sec,
        level_strings[level]
    );

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
}
