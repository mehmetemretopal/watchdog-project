#define _POSIX_C_SOURCE 200809L
/*
 * Test worker.
 *   worker normal <sn>   : <sn> saniye calisir, exit(0)
 *   worker crash  <sn>   : <sn> saniye calisir, exit(1)
 *   worker loop          : SIGTERM gelene kadar calisir
 * SIGTERM alirsa temiz cikar (exit 0).
 * Not: SIGKILL ile oldurulme testi disaridan yapilir.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_term(int sig)
{
    (void)sig;
    g_stop = 1;
}

int main(int argc, char *argv[])
{
    const char *mode = (argc > 1) ? argv[1] : "loop";
    int secs = (argc > 2) ? atoi(argv[2]) : 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    printf("worker[%d] started, mode=%s secs=%d\n", (int)getpid(), mode, secs);
    fflush(stdout);

    int elapsed = 0;
    while (!g_stop) {
        if (strcmp(mode, "loop") != 0 && elapsed >= secs) {
            int code = (strcmp(mode, "crash") == 0) ? 1 : 0;
            printf("worker[%d] exiting with code %d\n", (int)getpid(), code);
            fflush(stdout);
            return code;
        }
        sleep(1);       /* SIGTERM sleep'i keser, dongu bayragi gorur */
        elapsed++;
    }

    printf("worker[%d] got SIGTERM, exiting cleanly\n", (int)getpid());
    fflush(stdout);
    return 0;
}