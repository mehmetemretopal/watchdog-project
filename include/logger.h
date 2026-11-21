#ifndef LOGGER_H
#define LOGGER_H

/* Log seviyeleri */
typedef enum { LOG_LVL_INFO, LOG_LVL_WARN, LOG_LVL_ERROR } log_level_t;

/* Log dosyasini append modunda acar. Basarisizsa -1 doner. */
int  logger_init(const char *path);

/* Dosyayi kapatir. */
void logger_close(void);

/* Zaman damgali, printf bicimli satir yazar: [YYYY-MM-DD HH:MM:SS] [LEVEL] mesaj
 * SADECE ana akistan cagrilmali, signal handler icinden ASLA. */
void logger_log(log_level_t level, const char *fmt, ...);

#endif