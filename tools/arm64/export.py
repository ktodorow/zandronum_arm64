#!/usr/bin/env python3
"""Verify client build and export artifacts plus actual ELF/ABI evidence."""
import ctypes
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

SOURCE = Path('/src')
BUILD = Path('/build')
OUT = Path('/export')


def command(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def main():
    OUT.mkdir()
    evidence = OUT / 'evidence'
    evidence.mkdir()
    cache = (BUILD / 'CMakeCache.txt').read_text()
    native_gles = 'ZANDRONUM_USE_GLES:BOOL=ON' in cache
    executable = 'zandronum-gles' if native_gles else 'zandronum'
    binary = BUILD / executable
    header = command('readelf', '-h', str(binary))
    assert 'AArch64' in header, header
    commands = json.loads((BUILD / 'compile_commands.json').read_text())
    assert commands and all('-march=armv8-a' in item['command'] for item in commands)
    assert not any(re.search(r'-march=native|-mcpu=native|-marm|-mfpu=|-mfloat-abi=|SERVER_ONLY', item['command']) for item in commands)
    assert any('oalsound.cpp' in item['file'] for item in commands)
    for setting in ('SERVERONLY:BOOL=OFF', 'NO_SOUND:BOOL=OFF', 'USE_OPENAL:BOOL=ON', 'NO_GL:BOOL=OFF'):
        assert setting in cache, setting
    for name in (executable, executable + '.debug', 'zandronum.pk3', 'brightmaps.pk3', 'skulltag_actors.pk3'):
        shutil.copy2(BUILD / name, OUT / name)
        if name.endswith('.pk3'):
            with zipfile.ZipFile(OUT / name) as archive:
                assert archive.testzip() is None, name
    shutil.copy2('/toolchain-packages.tsv', evidence)
    for name in ('CMakeCache.txt', 'compile_commands.json', 'build.log'):
        shutil.copy2(BUILD / name, evidence / name)
    for name in ('build-source.json', 'source-provenance.txt'):
        shutil.copy2(SOURCE / name, evidence / name)
    shutil.copy2(SOURCE / 'src/gitinfo.h', evidence / 'gitinfo.h')
    version = command(str(binary), '--version')
    assert '3.2.1' in version, version
    (evidence / 'client-version.txt').write_text(version)
    ldd = command('ldd', str(binary))
    assert 'not found' not in ldd, ldd
    (evidence / 'ldd.txt').write_text(ldd)
    paths = {binary}
    paths.update(Path(path) for path in re.findall(r'(/\S+)', ldd))
    # SDL 1.2 compatibility loads SDL2 using dlopen; ldd alone omits it.
    sdl = ctypes.CDLL('libSDL-1.2.so.0')
    assert sdl.SDL_Init(0) == 0
    sdl.SDL_Quit()
    maps = Path('/proc/self/maps').read_text()
    paths.update(Path(line.split()[-1]) for line in maps.splitlines() if '/libSDL' in line)
    (evidence / 'sdl-loader-maps.txt').write_text('\n'.join(line for line in maps.splitlines() if '/libSDL' in line) + '\n')
    sdl2 = ctypes.CDLL('libSDL2-2.0.so.0')
    symbols = json.loads((SOURCE / 'tools/arm64/sdl2-symbols.json').read_text())
    if native_gles:
        symbols['native_gles_bridge_source'] = 'src/sdl/sdlglvideo.cpp'
        symbols['native_gles_bridge_symbols'] = [
            'SDL_GL_SetAttribute', 'SDL_GL_GetAttribute', 'SDL_GL_SetSwapInterval',
            'SDL_GL_GetSwapInterval', 'SDL_GL_GetCurrentWindow', 'SDL_GetError',
        ]
        symbols['sdl2_required_symbols'] = sorted(set(
            symbols['sdl2_required_symbols'] + symbols['native_gles_bridge_symbols']))
    assert all(hasattr(sdl2, name) for name in symbols['sdl2_required_symbols'])
    (evidence / 'sdl2-required-symbols.json').write_text(json.dumps(symbols, indent=2) + '\n')
    for path in tuple(paths):
        if 'libSDL2' in path.name:
            closure = command('ldd', str(path))
            assert 'not found' not in closure, closure
            (evidence / 'sdl2-ldd.txt').write_text(closure)
            paths.update(Path(item) for item in re.findall(r'(/\S+)', closure))
    graphics = (SOURCE / 'src/gl/system/gl_interface.cpp').read_text()
    graphics_symbols = {
        'note': 'Extension/GL-version conditional lookups; verify with actual device context.',
        'source': 'src/gl/system/gl_interface.cpp',
        'proc_address_symbols': sorted(set(re.findall(r'GetProcAddress\("([^"]+)"', graphics))),
    }
    (evidence / 'graphics-dynamic-symbols.json').write_text(json.dumps(graphics_symbols, indent=2) + '\n')
    fluid_source = 'src/sound/music_fluidsynth_mididevice.cpp'
    fluid = (SOURCE / fluid_source).read_text()
    fluid_symbols = {
        'note': 'Optional MIDI backend; not installed or runtime-tested in builder.',
        'source': fluid_source,
        'library': re.search(r'#define FLUIDSYNTHLIB\s+"([^"]+)"', fluid).group(1),
        'required_symbols': sorted(set(re.findall(r'\(void \*\*\)&\w+,\s*"([^"]+)"', fluid))),
    }
    (evidence / 'fluidsynth-dynamic-symbols.json').write_text(json.dumps(fluid_symbols, indent=2) + '\n')
    private = OUT / 'lib'
    private.mkdir()
    for path in paths:
        if path.name.startswith('libSDL-1.2') or 'libSDL1.2-compat' in path.name:
            shutil.copy2(path.resolve(), private / 'libSDL-1.2.so.0')
    assert (private / 'libSDL-1.2.so.0').exists()
    dependencies = []
    for index, path in enumerate(sorted({path.resolve() for path in paths})):
        prefix = f'{index:03d}-{path.name}'
        entry = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        for label, flags in (
            ('header', ['-h']), ('dynamic', ['-dW']),
            ('versions', ['-VW']), ('symbols', ['--dyn-syms', '-W']),
        ):
            report = command('readelf', *flags, str(path))
            (evidence / (prefix + '.' + label + '.txt')).write_text(report)
            if label == 'dynamic':
                entry['needed'] = re.findall(r'\(NEEDED\).*?\[(.*?)\]', report)
            if label == 'symbols':
                entry['undefined_symbols'] = sorted(set(re.findall(r'\bUND\s+(\S+)', report)))
        dependencies.append(entry)
    (evidence / 'dependencies.json').write_text(json.dumps(dependencies, indent=2) + '\n')
    client = next(entry for entry in dependencies if entry['path'] == str(binary))
    if native_gles:
        assert 'libGLESv2.so.2' in client['needed'], client['needed']
        assert not any(name.startswith(('libGL.so', 'libGLU.', 'libGLEW.', 'libOpenGL.'))
                       for name in client['needed']), client['needed']
    versions = sorted({symbol.split('@')[-1] for symbol in client['undefined_symbols'] if '@' in symbol})
    (evidence / 'client-abi.json').write_text(json.dumps({
        'machine': 'AArch64', 'needed': client['needed'], 'required_symbol_versions': versions,
        'renderer': 'gles' if native_gles else 'desktop-gl', 'executable': executable,
        'note': 'Client imports only; dependency reports cover each library separately.',
    }, indent=2) + '\n')
    tools = {
        'gcc': command('gcc', '--version').splitlines()[0],
        'cmake': command('cmake', '--version').splitlines()[0],
        'target': command('gcc', '-dumpmachine').strip(),
        'cpu_flags': '-march=armv8-a -mtune=cortex-a35 -fsigned-char',
        'audio': 'OpenAL + libsndfile + mpg123; FMOD VoIP unavailable',
        'graphics': 'native GLES2, firmware EGL/GLES' if native_gles else 'desktop OpenGL',
        'runtime_status': 'build/loader checks only; R36S graphics/audio/input untested',
    }
    (evidence / 'build.json').write_text(json.dumps(tools, indent=2) + '\n')
    hashes = {str(path.relative_to(OUT)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in sorted(OUT.rglob('*')) if path.is_file()}
    (OUT / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()))


if __name__ == '__main__':
    main()
