#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>

#define MAX_RESTARTS      3   /* bir child icin en fazla yeniden baslatma */
#define MAX_CHILDREN      8
#define SHUTDOWN_TIMEOUT  5   /* saniye: SIGTERM sonrasi SIGKILL'e kadar */
#define CHECK_INTERVAL    1   /* saniye: SIGALRM periyodu */

typedef enum {
    PS_NOT_STARTED,
    PS_RUNNING,
    PS_EXITED_NORMAL,   /* exit(0): normal cikis, restart YOK */
    PS_FAILED,          /* restart limiti asildi, vazgecildi */
    PS_STOPPED          /* graceful shutdown ile durduruldu */
} proc_state_t;

typedef struct {
    const char  *name;
    const char  *path;          /* exec edilecek dosya */
    char *const *argv;
    pid_t        pid;
    proc_state_t state;
    int          restart_count;
    int          last_status;   /* waitpid status */
} child_t;

/* Child tablosunu hazirlar (en az 2 worker). */
void pm_init(void);

/* Tum child'lari fork()+exec() ile baslatir. */
void pm_start_all(void);

/* SIGCHLD geldiginde cagrilir: waitpid(-1, WNOHANG) dongusu,
 * normal/beklenmedik cikis ayrimi, sinirli restart. */
void pm_reap_children(void);

/* SIGALRM geldiginde cagrilir: periyodik saglik kontrolu (kill(pid,0)). */
void pm_periodic_check(void);

/* setitimer ile periyodik SIGALRM baslatir. */
void pm_start_timer(void);

/* Shutdown: tum calisan child'lara SIGTERM. */
void pm_terminate_all(void);

/* Timeout dolduysa hala yasayanlara SIGKILL. */
void pm_kill_remaining(void);

/* Hala calisan child sayisi. */
int  pm_running_count(void);

/* SIGUSR1 raporu: her child'in PID ve durumunu loga yazar. */
void pm_report_status(void);

#endif