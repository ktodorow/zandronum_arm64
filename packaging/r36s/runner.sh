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
cleanup() {
    status=$?
    trap - EXIT INT TERM HUP
    [[ -n "$game_pid" ]] && kill -TERM "$game_pid" 2>/dev/null
    [[ -n "$game_pid" ]] && wait "$game_pid" 2>/dev/null
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

iwad=${ZANDRONUM_IWAD:-}
if [[ -z "$iwad" ]]; then
    for name in doom2.wad doom.wad; do
        for location in "$gamedir/iwads" "/$directory/doom"; do
            candidate=$(find "$location" -maxdepth 1 -type f -iname "$name" -print -quit 2>/dev/null)
            [[ -n "$candidate" ]] && { iwad=$candidate; break 2; }
        done
    done
fi
if [[ ! -f "$iwad" ]]; then
    requirement="Doom data required. Copy your own doom.wad or doom2.wad to /$directory/doom or $gamedir/iwads. No game data downloaded."
    echo "$requirement"
    pm_show_error "$requirement"
    exit 2
fi

renderer=${ZANDRONUM_RENDERER:-$(cat renderer.txt 2>/dev/null)}
renderer=${renderer:-gl4es}
export LD_LIBRARY_PATH="$gamedir/libs.$DEVICE_ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_EGL_DRIVER=/usr/lib/aarch64-linux-gnu/libEGL.so.1
export SDL12COMPAT_OPENGL_SCALING=0 SDL12COMPAT_DEBUG_LOGGING=1
export SDL12COMPAT_USE_GAME_CONTROLLERS=1
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
        ;;
    *) echo "ERROR: renderer must be gl4es or software, got $renderer"; exit 2 ;;
esac
echo "Renderer=$renderer internal=640x480 display=${DISPLAY_WIDTH}x${DISPLAY_HEIGHT} SDL=$SDL_VIDEODRIVER"
echo "IWAD=$iwad"
sha256sum "$iwad" zandronum *.pk3
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH EGL=$SDL_VIDEO_EGL_DRIVER"
python3 check-runtime.py || exit 1
[[ -f config/zandronum.ini ]] || cp defaults.ini config/zandronum.ini

# Migrate #3 menu-only configuration once; retain owner changes on relaunch.
# Marker is archived by engine with bindings, so failed initialization retries.
profile_args=()
if ! grep -qx 'r36s_controls_profile=1' config/zandronum.ini; then
    if [[ ! -e config/zandronum.before-controls-v1.ini ]]; then
        cp config/zandronum.ini config/zandronum.before-controls-v1.ini || exit 1
    fi
    profile_args=(-exec "$gamedir/controls.cfg")
    echo 'Installing controls profile 1; original configuration backed up.'
else
    echo 'Keeping saved controls/settings (profile 1 already installed).'
fi
echo "Configuration=$gamedir/config/zandronum.ini Saves=$gamedir/saves"
# Helper supplies keyboard/mouse input; native joystick stays disabled.
pm_platform_helper
# Installed helper uses pkill -f; match only client's explicit argv[0].
"$controlfolder/gptokeyb" -k '^\./zandronum( |$)' -hotkey back -c "$gamedir/gameplay.gptk" &
helper_pid=$!
./zandronum -iwad "$iwad" -config "$gamedir/config/zandronum.ini" \
    -savedir "$gamedir/saves" -width 640 -height 480 \
    "${profile_args[@]}" +vid_renderer "$renderer_id" +fullscreen 1 +use_joystick 0 "$@" &
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
