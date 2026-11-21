#define _POSIX_C_SOURCE 200809L
#include "logger.h"
#include "process_manager.h"
#include "signal_manager.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define LOG_FILE "watchdog.log"

/* Graceful shutdown: SIGTERM gonder, SHUTDOWN_TIMEOUT sn bekle,
 * hala yasayan varsa SIGKILL, sonra hepsini reap et. */
static void graceful_shutdown(void)
{
    logger_log(LOG_LVL_INFO, "Shutdown requested: stopping %d running child(ren)",
               pm_running_count());

    pm_terminate_all();                 /* SIGTERM + yeni restart yok */

    time_t deadline = time(NULL) + SHUTDOWN_TIMEOUT;
    int killed = 0;

    for (;;) {
        /* Once topla, sonra bekle. Sinyaller bloklu oldugu icin bu sirada
         * gelen SIGCHLD kaybolmaz, sigsuspend hemen uyanir. */
        g_got_sigchld = 0;
        pm_reap_children();
        if (pm_running_count() == 0)
            break;

        if (!killed && time(NULL) >= deadline) {
            pm_kill_remaining();        /* kontrollu SIGKILL */
            killed = 1;
        }
        sm_wait_for_signal();           /* SIGCHLD veya 1 sn'lik SIGALRM */
    }

    logger_log(LOG_LVL_INFO, "All children reaped, watchdog shutting down");
}

int main(void)
{
    if (logger_init(LOG_FILE) < 0)
        return 1;

    if (sm_install_handlers() < 0) {
        logger_log(LOG_LVL_ERROR, "Failed to install signal handlers");
        logger_close();
        return 1;
    }

    logger_log(LOG_LVL_INFO, "Watchdog started (pid %d)", (int)getpid());
    printf("watchdog started, pid=%d, log=%s\n", (int)getpid(), LOG_FILE);
    fflush(stdout);

    pm_init();
    pm_start_all();
    pm_start_timer();

    /* Ana kontrol dongusu: handler'lar sadece bayrak set eder,
     * asil is burada yapilir. */
    while (!g_shutdown_requested) {
        sm_wait_for_signal();

        if (g_got_sigchld) {
            g_got_sigchld = 0;
            pm_reap_children();
        }
        if (g_got_sigalrm) {
            g_got_sigalrm = 0;
            pm_periodic_check();
        }
        if (g_got_sigusr1) {
            g_got_sigusr1 = 0;
            pm_report_status();
        }
    }

    graceful_shutdown();
    logger_close();
    return 0;
}