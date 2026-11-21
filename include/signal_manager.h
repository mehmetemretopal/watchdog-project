#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

#include <signal.h>

/* Handler'lar sadece bu bayraklari set eder (async-signal-safe). */
extern volatile sig_atomic_t g_got_sigchld;
extern volatile sig_atomic_t g_got_sigalrm;
extern volatile sig_atomic_t g_got_sigusr1;
extern volatile sig_atomic_t g_shutdown_requested;

/* sigaction() ile SIGCHLD, SIGALRM, SIGUSR1, SIGINT, SIGTERM kurar.
 * Kurulumdan sonra bu sinyaller bloklu kalir; sigsuspend ile acilir. */
int  sm_install_handlers(void);

/* Sinyalleri atomik olarak acip bir sinyal gelene kadar bekler
 * (sigsuspend). Race condition olmasin diye pause() yerine bu. */
void sm_wait_for_signal(void);

/* fork() sonrasi CHILD icinde, exec()'ten once cagrilir:
 * bloklu sinyal maskesini eski haline getirir (maske exec'te miras kalir). */
void sm_unblock_for_child(void);

#endif