#define _POSIX_C_SOURCE 200809L
#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG_LINE_MAX 512

static int g_log_fd = -1;

static const char *level_str(log_level_t l)
{
    switch (l) {
    case LOG_LVL_INFO:  return "INFO";
    case LOG_LVL_WARN:  return "WARN";
    case LOG_LVL_ERROR: return "ERROR";
    }
    return "?";
}

int logger_init(const char *path)
{
    g_log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_log_fd < 0) {
        fprintf(stderr, "logger: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

void logger_close(void)
{
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
}

void logger_log(log_level_t level, const char *fmt, ...)
{
    char line[LOG_LINE_MAX];
    char ts[32];
    time_t now = time(NULL);
    struct tm tm_buf;

    if (g_log_fd < 0)
        return;

    localtime_r(&now, &tm_buf);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm_buf);

    int n = snprintf(line, sizeof line, "[%s] [%s] ", ts, level_str(level));
    if (n < 0 || n >= (int)sizeof line)
        return;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (m < 0)
        return;

    size_t len = strlen(line);
    line[len++] = '\n';

    /* Tek write() = satir butunlugu; EINTR olursa tekrar dene. */
    ssize_t w;
    do {
        w = write(g_log_fd, line, len);
    } while (w < 0 && errno == EINTR);
}