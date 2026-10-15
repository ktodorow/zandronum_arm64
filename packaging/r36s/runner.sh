#!/bin/bash
# Installed PortMaster hooks are not compatible with errexit/nounset.
gamedir="$(cd -- "$(dirname -- "$0")" && pwd)"
cd "$gamedir" || exit 1
mkdir -p logs config saves
[[ -s log.txt ]] && cp log.txt "logs/$(date +%Y%m%d-%H%M%S)-previous.log"
: > log.txt
echo "Zandronum R36S launch $(date -Is)"
cat /proc/$$/cgroup
if grep -q 'emulationstation.service' /proc/$$/cgroup; then
    echo 'ERROR: runner still belongs to EmulationStation; refusing to stop frontend.'
    exit 1
fi

for controlfolder in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
    "${XDG_DATA_HOME:-$HOME/.local/share}/PortMaster" /roms2/ports/PortMaster /roms/ports/PortMaster; do
    [[ -f "$controlfolder/control.txt" ]] && break
done
if [[ ! -f "$controlfolder/control.txt" ]]; then
    echo 'ERROR: installed PortMaster control.txt not found.'
    exit 1
fi
source "$controlfolder/control.txt"
[[ -f "$controlfolder/mod_${CFW_NAME}.txt" ]] && source "$controlfolder/mod_${CFW_NAME}.txt"

game_pid=''
helper_pid=''
selection_file=''
cleanup() {
    status=$?
    trap - EXIT INT TERM HUP
    [[ -n "$game_pid" ]] && kill -TERM "$game_pid" 2>/dev/null
    [[ -n "$game_pid" ]] && wait "$game_pid" 2>/dev/null
    [[ -n "$selection_file" ]] && rm -f -- "$selection_file"
    # Scope PortMaster's finish hook to this package's helper.
    pm_gptokeyb_finish() {
        if [[ -n "$helper_pid" ]]; then
            kill -TERM "$helper_pid" 2>/dev/null
            wait "$helper_pid" 2>/dev/null
        fi
        return 0
    }
    pm_finish
    echo "Game/runner exit status: $status"
    /bin/bash "$gamedir/restore-frontend.sh"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

echo 'Before ES stop:'
systemctl show emulationstation.service -p ActiveState -p SubState -p MainPID
free -m
sudo -n systemctl stop emulationstation.service || exit 1
if systemctl is-active --quiet emulationstation.service || pgrep -x emulationstatio >/dev/null; then
    echo 'ERROR: EmulationStation still running; client not started.'
    exit 1
fi
echo 'After ES stop:'
systemctl show emulationstation.service -p ActiveState -p SubState -p MainPID
free -m

get_controls
[[ "$DEVICE_ARCH" == aarch64 ]] || { echo "ERROR: expected aarch64, got $DEVICE_ARCH"; exit 1; }
echo "PortMaster=$PM_VERSION CFW=$CFW_NAME/$CFW_VERSION controlfolder=$controlfolder ports=/$directory/ports"
echo "Controller=$DEVICE mapping=$sdl_controllerconfig"

renderer=${ZANDRONUM_RENDERER:-$(cat renderer.txt 2>/dev/null)}
renderer=${renderer:-gl4es}
save_key=base
addon_args=()
export LD_LIBRARY_PATH="$gamedir/libs.$DEVICE_ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_EGL_DRIVER=/usr/lib/aarch64-linux-gnu/libEGL.so.1
export SDL12COMPAT_OPENGL_SCALING=0 SDL12COMPAT_DEBUG_LOGGING=1
export SDL12COMPAT_USE_GAME_CONTROLLERS=1

iwad=${ZANDRONUM_IWAD:-}
# Explicit IWAD/engine arguments retain scripted diagnostic launches.
if [[ ${ZANDRONUM_LAUNCHER:-1} != 0 && -z "$iwad" && $# == 0 ]]; then
    selection_file=$(mktemp "$gamedir/config/launcher-selection.XXXXXX") || exit 1
    SDL_RENDER_DRIVER=opengles2 ./r36s-launcher --game-dir "$gamedir" \
        --content-root "/$directory/doom" --renderer "$renderer" --output "$selection_file" &
    game_pid=$!
    wait "$game_pid"
    status=$?
    game_pid=''
    [[ $status == 10 ]] && exit 0
    if [[ $status != 0 ]]; then
        pm_show_error 'Zandronum content menu failed. See zandronum/log.txt.'
        exit "$status"
    fi
    mapfile -d '' -t selection < "$selection_file"
    rm -f -- "$selection_file"
    selection_file=''
    if (( ${#selection[@]} < 3 || ${#selection[@]} > 131 )) ||
        [[ ! ${selection[0]} =~ ^(gl4es|software)$ ]] ||
        [[ ${selection[1]} != /* || ! -f ${selection[1]} ]] ||
        [[ ! ${selection[2]} =~ ^(base|set-[0-9a-f]{16})$ ]]; then
        echo 'ERROR: invalid content-menu selection.'
        exit 2
    fi
    renderer=${selection[0]}
    iwad=${selection[1]}
    save_key=${selection[2]}
    if (( ${#selection[@]} > 3 )); then
        for addon in "${selection[@]:3}"; do
            [[ "$addon" == /* && -f "$addon" ]] || exit 2
        done
        addon_args=(-file "${selection[@]:3}")
    fi
fi
if [[ -z "$iwad" ]]; then
    for name in doom2.wad doom.wad; do
        for location in "$gamedir/iwads" "/$directory/doom"; do
            candidate=$(find "$location" -maxdepth 1 -type f -iname "$name" -print -quit 2>/dev/null)
            [[ -n "$candidate" ]] && { iwad=$candidate; break 2; }
        done
    done
fi
if [[ ! -f "$iwad" ]]; then
    requirement="Base game file missing. Copy your own WAD or PK3 to /$directory/doom or $gamedir/iwads, then choose it in content menu or set ZANDRONUM_IWAD to its path. No game data downloaded."
    echo "$requirement"
    pm_show_error "$requirement"
    exit 2
fi

save_directory="$gamedir/saves"
case "$renderer" in
    gl4es)
        graphics_hook="$controlfolder/libgl_${CFW_NAME}.txt"
        [[ -f "$graphics_hook" ]] || graphics_hook="$controlfolder/libgl_default.txt"
        source "$graphics_hook"
        export LIBGL_EGL=/usr/lib/aarch64-linux-gnu/libEGL.so.1
        export LIBGL_GLES=/usr/lib/aarch64-linux-gnu/libGLESv2.so.2
        export SDL_VIDEO_GL_DRIVER="$gamedir/gl4es.$DEVICE_ARCH/libGL.so.1"
        export SDL_VIDEO_EGL_DRIVER="$gamedir/gl4es.$DEVICE_ARCH/libEGL.so.1"
        renderer_id=1
        ;;
    software)
        export SDL_RENDER_DRIVER=opengles2
        renderer_id=0
        # Engine rejects GL saves; keep original saves and autosaves separate.
        save_directory="$gamedir/saves/software"
        ;;
    *) echo "ERROR: renderer must be gl4es or software, got $renderer"; exit 2 ;;
esac
config_file="$gamedir/config/zandronum.ini"
[[ -f "$config_file" ]] || cp defaults.ini "$config_file" || exit 1
if [[ "$save_key" != base ]]; then
    config_file="$gamedir/config/profiles/$save_key/zandronum.ini"
    save_directory="$gamedir/saves/profiles/$save_key/$renderer"
    mkdir -p "$(dirname -- "$config_file")" || exit 1
    [[ -f "$config_file" ]] || cp "$gamedir/config/zandronum.ini" "$config_file" || exit 1
fi
mkdir -p "$save_directory" || exit 1
echo "Renderer=$renderer internal=640x480 display=${DISPLAY_WIDTH}x${DISPLAY_HEIGHT} SDL=$SDL_VIDEODRIVER"
echo "IWAD=$iwad"
sha256sum "$iwad" zandronum *.pk3
(( ${#addon_args[@]} )) && sha256sum "${addon_args[@]:1}"
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH EGL=$SDL_VIDEO_EGL_DRIVER"
python3 check-runtime.py || exit 1

# Migrate #3 menu-only configuration once; retain owner changes on relaunch.
# Marker is archived by engine with bindings, so failed initialization retries.
profile_args=()
if ! grep -qx 'r36s_controls_profile=1' "$config_file"; then
    if [[ ! -e "${config_file%.ini}.before-controls-v1.ini" ]]; then
        cp "$config_file" "${config_file%.ini}.before-controls-v1.ini" || exit 1
    fi
    profile_args+=(-exec "$gamedir/controls.cfg")
    echo 'Installing controls profile 1; original configuration backed up.'
else
    echo 'Keeping saved controls/settings (profile 1 already installed).'
fi
if ! grep -qx 'r36s_audio_profile=2' "$config_file"; then
    if [[ ! -e "${config_file%.ini}.before-audio-v2.ini" ]]; then
        cp "$config_file" "${config_file%.ini}.before-audio-v2.ini" || exit 1
    fi
    profile_args+=(-exec "$gamedir/audio.cfg")
    echo 'Installing audio profile 2; original configuration backed up.'
else
    echo 'Keeping saved audio settings (profile 2 already installed).'
fi
echo 'Audio=OpenAL Soft/ALSA; built-in DBOPL default; saved engine volumes retained.'
echo "Configuration=$config_file Saves=$save_directory ContentSet=$save_key"
# Helper supplies keyboard/mouse input; native joystick stays disabled.
pm_platform_helper
# Installed helper uses pkill -f; match only client's explicit argv[0].
"$controlfolder/gptokeyb" -k '^\./zandronum( |$)' -hotkey back -c "$gamedir/gameplay.gptk" &
helper_pid=$!
./zandronum -iwad "$iwad" -config "$config_file" \
    -savedir "$save_directory" -width 640 -height 480 \
    "${profile_args[@]}" "${addon_args[@]}" +vid_renderer "$renderer_id" +fullscreen 1 +use_joystick 0 "$@" &
game_pid=$!
sleep 2
if kill -0 "$game_pid" 2>/dev/null; then
    echo 'Loaded runtime libraries:'
    awk '/\.so/ {print $NF}' "/proc/$game_pid/maps" | sort -u
fi
wait "$game_pid"
status=$?
game_pid=''
exit "$status"
