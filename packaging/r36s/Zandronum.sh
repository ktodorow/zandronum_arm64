#!/bin/bash
# Personal dArkOS package. Always detach, including SSH-driven tests.
set -e
gamedir="$(cd -- "$(dirname -- "$0")" && pwd)/zandronum"
mkdir -p "$gamedir/logs"

# A fixed unit also rejects concurrent launches before either can stop ES.
# ExecStopPost restores ES even if runner is killed or fails before its trap.
env_args=()
for name in HOME USER LOGNAME PATH LANG TERM XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS \
    SDL_KMSDRM_DEVICE_INDEX SDL_KMSDRM_ORIENTATION SDL_KMSDRM_ROTATION \
    ZANDRONUM_RENDERER ZANDRONUM_IWAD; do
    [[ -n ${!name+x} ]] && env_args+=(--setenv="$name=${!name}")
done
sudo -n systemd-run --unit=zandronum-r36s --collect \
    --uid="$(id -u)" --gid="$(id -g)" \
    --property="SupplementaryGroups=$(id -Gn)" \
    --property=KillMode=control-group --property=TimeoutStopSec=15 \
    --property="StandardOutput=append:$gamedir/log.txt" \
    --property="StandardError=append:$gamedir/log.txt" \
    --property="ExecStopPost=+/bin/bash \"$gamedir/restore-frontend.sh\"" \
    "${env_args[@]}" /bin/bash "$gamedir/runner.sh" "$@"
