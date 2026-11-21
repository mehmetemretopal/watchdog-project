#define _POSIX_C_SOURCE 200809L
#include "process_manager.h"
#include "logger.h"
#include "signal_manager.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define WORKER_PATH "./worker"

/* Worker argumanlari: test senaryolarini tetikleyecek sekilde secildi. */
static char *const argv_loop[]   = { "worker", "loop",        NULL };
static char *const argv_normal[] = { "worker", "normal", "15", NULL };
static char *const argv_crash[]  = { "worker", "crash",  "3",  NULL };

static child_t g_children[MAX_CHILDREN];
static int     g_nchildren = 0;
static int     g_shutting_down = 0;   /* 1 olunca yeni process baslatilmaz */

static const char *state_str(proc_state_t s)
{
    switch (s) {
    case PS_NOT_STARTED:  return "NOT_STARTED";
    case PS_RUNNING:      return "RUNNING";
    case PS_EXITED_NORMAL:return "EXITED_NORMAL";
    case PS_FAILED:       return "FAILED(restart limit)";
    case PS_STOPPED:      return "STOPPED";
    }
    return "UNKNOWN";
}

static void add_child(const char *name, char *const argv[])
{
    if (g_nchildren >= MAX_CHILDREN)
        return;
    child_t *c = &g_children[g_nchildren++];
    memset(c, 0, sizeof *c);
    c->name  = name;
    c->path  = WORKER_PATH;
    c->argv  = argv;
    c->pid   = -1;
    c->state = PS_NOT_STARTED;
}

void pm_init(void)
{
    g_nchildren = 0;
    g_shutting_down = 0;
    add_child("worker-loop",   argv_loop);    /* surekli calisir (kill -9 testi icin) */
    add_child("worker-normal", argv_normal);  /* 15 sn sonra exit(0) */
    add_child("worker-crash",  argv_crash);   /* 3 sn sonra exit(1): restart limiti testi */
}

/* fork() + execv(). Basarili ise 0, degilse -1. */
static int spawn(child_t *c)
{
    pid_t pid = fork();
    if (pid < 0) {
        logger_log(LOG_LVL_ERROR, "fork failed for %s: %s", c->name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* CHILD: sinyal maskesi exec'te miras kalir, once temizle. */
        sm_unblock_for_child();
        execv(c->path, c->argv);
        /* exec donduyse hata var; stdio'ya guvenme, _exit kullan. */
        const char msg[] = "exec failed\n";
        ssize_t r = write(STDERR_FILENO, msg, sizeof msg - 1);
        (void)r;
        _exit(127);
    }
    c->pid   = pid;
    c->state = PS_RUNNING;
    return 0;
}

void pm_start_all(void)
{
    for (int i = 0; i < g_nchildren; i++) {
        child_t *c = &g_children[i];
        if (spawn(c) == 0)
            logger_log(LOG_LVL_INFO, "Started %s (pid %d)", c->name, (int)c->pid);
        else
            c->state = PS_FAILED;
    }
}

static child_t *find_by_pid(pid_t pid)
{
    for (int i = 0; i < g_nchildren; i++)
        if (g_children[i].pid == pid && g_children[i].state == PS_RUNNING)
            return &g_children[i];
    return NULL;
}

static void handle_exit(child_t *c, int status)
{
    pid_t old_pid = c->pid;
    c->last_status = status;
    c->pid = -1;

    if (g_shutting_down) {
        c->state = PS_STOPPED;
        if (WIFSIGNALED(status))
            logger_log(LOG_LVL_INFO, "%s (pid %d) stopped by signal %d (%s) during shutdown",
                       c->name, (int)old_pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
        else
            logger_log(LOG_LVL_INFO, "%s (pid %d) stopped during shutdown (exit code %d)",
                       c->name, (int)old_pid, WEXITSTATUS(status));
        return;
    }

    /* Normal cikis: exit(0) -> restart YOK */
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        c->state = PS_EXITED_NORMAL;
        logger_log(LOG_LVL_INFO, "%s (pid %d) exited normally (code 0), not restarting",
                   c->name, (int)old_pid);
        return;
    }

    /* Beklenmedik cikis: hata kodu veya sinyal */
    if (WIFSIGNALED(status))
        logger_log(LOG_LVL_WARN, "%s (pid %d) terminated unexpectedly by signal %d (%s)",
                   c->name, (int)old_pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
    else
        logger_log(LOG_LVL_WARN, "%s (pid %d) terminated unexpectedly with exit code %d",
                   c->name, (int)old_pid, WEXITSTATUS(status));

    if (c->restart_count >= MAX_RESTARTS) {
        c->state = PS_FAILED;
        logger_log(LOG_LVL_ERROR, "%s reached restart limit (%d), giving up",
                   c->name, MAX_RESTARTS);
        return;
    }

    c->restart_count++;
    if (spawn(c) == 0)
        logger_log(LOG_LVL_INFO, "Restarted %s (new pid %d), restart %d/%d",
                   c->name, (int)c->pid, c->restart_count, MAX_RESTARTS);
    else
        c->state = PS_FAILED;
}

void pm_reap_children(void)
{
    /* Birden fazla SIGCHLD tek bayraga birlesebilir; bu yuzden
     * bekleyen TUM child'lar bitene kadar WNOHANG ile dongu. */
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            child_t *c = find_by_pid(pid);
            if (c)
                handle_exit(c, status);
            else
                logger_log(LOG_LVL_WARN, "Reaped unknown child pid %d", (int)pid);
        } else if (pid == 0) {
            break;                      /* calisan child var ama bitmemis */
        } else if (errno == EINTR) {
            continue;
        } else {
            break;                      /* ECHILD: child kalmadi */
        }
    }
}

void pm_periodic_check(void)
{
    /* Guvenlik agi: kacan bir SIGCHLD olursa burada toplanir. */
    pm_reap_children();

    for (int i = 0; i < g_nchildren; i++) {
        child_t *c = &g_children[i];
        if (c->state == PS_RUNNING && kill(c->pid, 0) < 0 && errno == ESRCH)
            logger_log(LOG_LVL_WARN, "%s (pid %d) not found in process table",
                       c->name, (int)c->pid);
    }
}

void pm_start_timer(void)
{
    struct itimerval it;
    it.it_interval.tv_sec  = CHECK_INTERVAL;
    it.it_interval.tv_usec = 0;
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_REAL, &it, NULL) < 0)
        logger_log(LOG_LVL_ERROR, "setitimer failed: %s", strerror(errno));
}

void pm_terminate_all(void)
{
    g_shutting_down = 1;               /* artik restart yok */
    for (int i = 0; i < g_nchildren; i++) {
        child_t *c = &g_children[i];
        if (c->state == PS_RUNNING) {
            if (kill(c->pid, SIGTERM) == 0)
                logger_log(LOG_LVL_INFO, "Sent SIGTERM to %s (pid %d)", c->name, (int)c->pid);
            else
                logger_log(LOG_LVL_WARN, "kill(SIGTERM) %s (pid %d): %s",
                           c->name, (int)c->pid, strerror(errno));
        }
    }
}

void pm_kill_remaining(void)
{
    for (int i = 0; i < g_nchildren; i++) {
        child_t *c = &g_children[i];
        if (c->state == PS_RUNNING) {
            kill(c->pid, SIGKILL);
            logger_log(LOG_LVL_WARN, "Timeout: sent SIGKILL to %s (pid %d)", c->name, (int)c->pid);
        }
    }
}

int pm_running_count(void)
{
    int n = 0;
    for (int i = 0; i < g_nchildren; i++)
        if (g_children[i].state == PS_RUNNING)
            n++;
    return n;
}

void pm_report_status(void)
{
    logger_log(LOG_LVL_INFO, "=== STATUS REPORT (%d children, %d running) ===",
               g_nchildren, pm_running_count());
    for (int i = 0; i < g_nchildren; i++) {
        child_t *c = &g_children[i];
        logger_log(LOG_LVL_INFO, "  %-14s pid=%-6d state=%-22s restarts=%d/%d",
                   c->name, (int)c->pid, state_str(c->state),
                   c->restart_count, MAX_RESTARTS);
    }
    logger_log(LOG_LVL_INFO, "=== END STATUS REPORT ===");
}