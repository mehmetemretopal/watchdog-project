#!/bin/bash
# Watchdog test senaryolari (odev Bolum 7).
# Kullanim: make && ./tests/test_scenarios.sh | tee docs/test_results.txt
cd "$(dirname "$0")/.." || exit 1

LOG=watchdog.log
PASS=0
FAIL=0
WPID=""

cleanup() {
    [ -n "$WPID" ] && kill -KILL "$WPID" 2>/dev/null
    pkill -KILL -x worker 2>/dev/null
}
trap cleanup EXIT

check() {   # check "aciklama" komut...
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "  [PASS] $desc"; PASS=$((PASS+1))
    else
        echo "  [FAIL] $desc"; FAIL=$((FAIL+1))
    fi
}

# wait_log "regex" saniye : logda regex gorunene kadar bekler
wait_log() {
    local i=0 max=$(( $2 * 5 ))
    while [ $i -lt $max ]; do
        grep -qE "$1" "$LOG" 2>/dev/null && return 0
        sleep 0.2; i=$((i+1))
    done
    return 1
}

start_watchdog() {
    rm -f "$LOG"
    ./watchdog >/dev/null 2>&1 &
    WPID=$!
    sleep 1
}

child_pid() {   # child_pid worker-adi : logdaki en son baslatilan PID
    grep -oE "(Started|Restarted) $1 \((new )?pid [0-9]+" "$LOG" | tail -1 | grep -oE '[0-9]+$'
}

count_log() { grep -cE "$1" "$LOG"; }

# PID dogrulama: gercekten bizim watchdog'un "worker" cocugu mu?
verify_worker_pid() {
    local pid="$1"
    [ "$(ps -o comm= -p "$pid" 2>/dev/null | tr -d ' ')" = "worker" ] &&
    [ "$(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' ')" = "$WPID" ]
}

[ -x ./watchdog ] && [ -x ./worker ] || { echo "Once 'make' calistirin."; exit 1; }

echo "=== Senaryo 1: Normal calisma ==="
start_watchdog
check "en az 2 child baslatildi (logda Started satirlari)" test "$(count_log 'Started worker-')" -ge 2
check "child'lar watchdog'un cocugu olarak calisiyor"      test "$(pgrep -P "$WPID" -c)" -ge 2
check "PID bilgileri loga yazildi"                         grep -qE "Started worker-loop \(pid [0-9]+\)" "$LOG"

echo "=== Senaryo 2: Beklenmedik sonlanma (SIGKILL) + restart ==="
LOOP_PID=$(child_pid worker-loop)
check "oldurulecek PID dogrulandi (worker + parent=watchdog)" verify_worker_pid "$LOOP_PID"
kill -KILL "$LOOP_PID"
check "beklenmedik sonlanma loglandi (signal 9)" wait_log "worker-loop \(pid $LOOP_PID\) terminated unexpectedly by signal 9" 3
check "worker-loop yeniden baslatildi"            wait_log "Restarted worker-loop" 3

echo "=== Senaryo 3: Restart limiti (worker-crash) ==="
check "restart limitine ulasildi ve vazgecildi" wait_log "worker-crash reached restart limit" 20
sleep 4
check "tam 3 restart yapildi, daha fazla degil" test "$(count_log 'Restarted worker-crash')" -eq 3

echo "=== Senaryo 4: Normal sonlanma (exit 0) ==="
check "worker-normal exit(0) ile bitti, restart edilmedi" wait_log "worker-normal .*exited normally" 20
check "worker-normal icin restart satiri yok"             test "$(count_log 'Restarted worker-normal')" -eq 0

echo "=== Senaryo 5: Durum raporu (SIGUSR1) ==="
kill -USR1 "$WPID"; sleep 1
check "rapor basligi loga yazildi"            grep -q "STATUS REPORT" "$LOG"
check "her child icin satir var (3 worker)"   test "$(grep -cE 'worker-(loop|normal|crash) +pid=' "$LOG")" -ge 3
check "durumlar dogru (RUNNING/EXITED_NORMAL/FAILED)" \
      bash -c "grep -q 'state=RUNNING' $LOG && grep -q 'state=EXITED_NORMAL' $LOG && grep -q 'state=FAILED' $LOG"

echo "=== Senaryo 6: Graceful shutdown (SIGTERM) ==="
kill -TERM "$WPID"
i=0; while kill -0 "$WPID" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
check "watchdog 10 sn icinde kapandi"        bash -c "! kill -0 $WPID 2>/dev/null"
wait "$WPID" 2>/dev/null; RC=$?; WPID=""
check "cikis kodu 0"                         test "$RC" -eq 0
check "kapanis loglandi"                     grep -q "All children reaped" "$LOG"
check "artik worker process'i kalmadi"       bash -c "! pgrep -x worker"
check "zombie (defunct) process kalmadi"     bash -c "! ps -eo stat,comm | grep -E '^Z.*worker'"

echo "=== Senaryo 6b: Ctrl+C (SIGINT) ayri test ==="
start_watchdog
kill -INT "$WPID"
i=0; while kill -0 "$WPID" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
check "SIGINT ile kapandi"                   bash -c "! kill -0 $WPID 2>/dev/null"
wait "$WPID" 2>/dev/null; RC=$?; WPID=""
check "cikis kodu 0"                         test "$RC" -eq 0
check "tum child'lar SIGTERM aldi"           test "$(count_log 'Sent SIGTERM')" -eq 3
check "worker kalmadi"                       bash -c "! pgrep -x worker"

echo "=== Senaryo 6c: SIGTERM'e cevap vermeyen child -> SIGKILL timeout ==="
start_watchdog
STUCK=$(child_pid worker-loop)
kill -STOP "$STUCK"                          # donmus process: SIGTERM'i isleyemez
kill -TERM "$WPID"
check "timeout sonrasi SIGKILL gonderildi (<=10 sn)" wait_log "Timeout: sent SIGKILL" 10
i=0; while kill -0 "$WPID" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
check "watchdog yine de kapandi"             bash -c "! kill -0 $WPID 2>/dev/null"
WPID=""
check "worker kalmadi"                       bash -c "! pgrep -x worker"

echo "=== Senaryo 7: Log dogrulama ==="
start_watchdog
kill -KILL "$(child_pid worker-loop)"; sleep 1; kill -USR1 "$WPID"; sleep 1
kill -TERM "$WPID"; wait "$WPID" 2>/dev/null; WPID=""
check "her satirda zaman damgasi var" \
      bash -c "! grep -vE '^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:]{8}\] \[(INFO|WARN|ERROR)\] ' $LOG"
for ev in "Watchdog started" "Started worker-" "terminated unexpectedly" "Restarted worker-" \
          "STATUS REPORT" "Shutdown requested" "Sent SIGTERM" "All children reaped"; do
    check "logda olay var: $ev" grep -q "$ev" "$LOG"
done

echo
echo "============================================"
echo " SONUC: $PASS gecti, $FAIL kaldi"
echo "============================================"
[ "$FAIL" -eq 0 ]