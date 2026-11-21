#define _POSIX_C_SOURCE 200809L
#include "signal_manager.h"

#include <stdio.h>
#include <string.h>

volatile sig_atomic_t g_got_sigchld = 0;
volatile sig_atomic_t g_got_sigalrm = 0;
volatile sig_atomic_t g_got_sigusr1 = 0;
volatile sig_atomic_t g_shutdown_requested = 0;

static sigset_t g_orig_mask;   /* sinyaller bloklanmadan onceki maske */

/* --- Handler'lar: sadece bayrak set eder, baska hicbir sey yapmaz --- */
static void on_sigchld(int sig) { (void)sig; g_got_sigchld = 1; }
static void on_sigalrm(int sig) { (void)sig; g_got_sigalrm = 1; }
static void on_sigusr1(int sig) { (void)sig; g_got_sigusr1 = 1; }
static void on_shutdown(int sig) { (void)sig; g_shutdown_requested = 1; }

static int install(int signo, void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(signo, &sa, NULL) < 0) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

int sm_install_handlers(void)
{
    sigset_t block;

    /* Once maskeyi blokla: handler kurulumu ile ilk bekleme arasinda
     * gelen sinyal kaybolmasin, bayrak kontrolu race'siz olsun. */
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigaddset(&block, SIGALRM);
    sigaddset(&block, SIGUSR1);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &block, &g_orig_mask) < 0) {
        perror("sigprocmask");
        return -1;
    }

    if (install(SIGCHLD, on_sigchld)  < 0) return -1;
    if (install(SIGALRM, on_sigalrm)  < 0) return -1;
    if (install(SIGUSR1, on_sigusr1)  < 0) return -1;
    if (install(SIGINT,  on_shutdown) < 0) return -1;
    if (install(SIGTERM, on_shutdown) < 0) return -1;
    return 0;
}

void sm_wait_for_signal(void)
{
    /* Maskeyi g_orig_mask ile degistirip uyur, sinyal gelince handler
     * calisir ve eski (bloklu) maske geri yuklenerek doner. Atomiktir. */
    sigsuspend(&g_orig_mask);
}

void sm_unblock_for_child(void)
{
    sigprocmask(SIG_SETMASK, &g_orig_mask, NULL);
}