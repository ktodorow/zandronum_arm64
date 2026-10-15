#!/bin/bash
# Used by both normal cleanup and systemd's crash recovery. No game libraries.
unset LD_LIBRARY_PATH LD_PRELOAD
printf '\nFrontend recovery: %s\n' "$(date -Is)"
if [[ -n ${SERVICE_RESULT:-} ]]; then
    printf 'Unit result: %s; exit code: %s; exit status: %s\n' \
        "$SERVICE_RESULT" "${EXIT_CODE:-unknown}" "${EXIT_STATUS:-unknown}"
fi
sudo -n systemctl reset-failed emulationstation.service || true
sudo -n systemctl start emulationstation.service || exit 1
for attempt in {1..30}; do
    if systemctl is-active --quiet emulationstation.service && pgrep -x emulationstatio >/dev/null; then
        systemctl show emulationstation.service -p ActiveState -p SubState -p MainPID
        free -m
        exit 0
    fi
    sleep 1
done
echo 'ERROR: EmulationStation process did not return; inspect systemctl status emulationstation.service.'
exit 1
