#!/usr/bin/env python3
"""Copy the persistent-fighter files from a wasm build into a tracked release folder.

    python3 wasm/release.py dist/wasm wasm/release

Writes fighter.mjs/.wasm, assembler.mjs/.wasm, fighter-client.mjs, a
deterministic source zip and manifest.json (SHA-256 of every file, the
compiler and the build commands) for corewar.dev to vendor with pinned hashes.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = ['fighter.mjs', 'fighter.wasm', 'assembler.mjs', 'assembler.wasm', 'fighter-client.mjs']
# Everything compiled into fighter.wasm/assembler.wasm, plus the wrapper and build scripts.
SOURCES = ['wasm/fighter.c', 'wasm/assembler.c', 'wasm/fighter-client.mjs', 'wasm/build.py', 'wasm/release.py',
           'wasm/README.md', 'pmars.c', 'sim.c', 'sim_loop.inc', 'pspace.c', 'exhaust.h', 'sim.h', 'insn.h',
           'insn_help.h', 'pspace.h', 'hill_config.h', 'Warrior.py', 'README.md']
ZIP_NAME = 'exmars-fighter-src.zip'
NOTICE = """exMARS persistent fighter and assembler for WebAssembly: corresponding source.

fighter.wasm is built from wasm/fighter.c, sim.c and pspace.c (exhaust by
M Joonas Pihlaja, public domain, and the exMARS fork's changes).
assembler.wasm additionally contains pmars.c, the pMARS parser, which is
GPL-2.0-or-later (see the notice at the top of pmars.c and COPYING, when included).
The build commands and compiler are listed in manifest.json; rebuild with
`make wasm` (Emscripten 4.0.23) and `make wasm-release`.
"""


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('build', type=Path)
    p.add_argument('release', type=Path)
    p.add_argument('--copying', type=Path, help='GPL text to include (default: the pMARS COPYING the build used)')
    a = p.parse_args()
    build, release = a.build.resolve(), a.release.resolve()
    manifest = json.loads((build / 'manifest.json').read_text())
    release.mkdir(parents=True, exist_ok=True)
    # Text files get LF line ends, as Git stores them, so the hashes below are the
    # same whichever OS made the release (a Windows checkout and emcc write CRLF).
    for name in FILES:
        data = (build / name).read_bytes()
        if not name.endswith('.wasm'):
            data = data.replace(b'\r\n', b'\n')
        (release / name).write_bytes(data)

    copying = a.copying
    if copying is None:
        candidate = build / 'sources/pmars/COPYING'
        copying = candidate if candidate.is_file() else None
    # Sources as committed at HEAD (the commit the manifest names), not the working tree.
    def committed(name):
        return subprocess.run(['git', '-C', str(ROOT), 'show', 'HEAD:' + name], capture_output=True, check=True).stdout

    dirty = subprocess.run(['git', '-C', str(ROOT), 'status', '--porcelain', '--', *SOURCES],
                           capture_output=True, text=True, check=True).stdout.strip()
    if dirty:
        raise SystemExit('Commit the sources first; uncommitted changes:\n' + dirty)
    sources = {name: committed(name) for name in SOURCES}
    entries = list(sources.items())
    entries.append(('NOTICE', NOTICE.encode()))
    if copying:
        entries.append(('COPYING', Path(copying).read_bytes().replace(b'\r\n', b'\n')))
    with zipfile.ZipFile(release / ZIP_NAME, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(entries):
            info = zipfile.ZipInfo('exmars-fighter-src/' + name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)

    def relative(text):
        """Portable form of a build argument: paths relative to the repository, with /."""
        text = text.replace(str(ROOT) + os.sep, '').replace(str(ROOT), '.')
        if Path(text).name.lower() in ('emcc', 'emcc.bat', 'emcc.py'):
            return 'emcc'
        text = text.replace('\\', '/')
        return text[2:] if text.startswith('./') else text

    commands = [[relative(c) for c in cmd] for cmd in manifest['commands']
                if any(Path(c).stem in ('fighter', 'assembler') for c in cmd)]
    head = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    (release / 'manifest.json').write_text(json.dumps({
        'compiler': manifest['compiler'].splitlines()[0],
        'builtFromParentCommit': head or None,
        'commands': commands,
        'sources': {name: hashlib.sha256(data).hexdigest() for name, data in sources.items()},
        'files': {name: sha(release / name) for name in FILES + [ZIP_NAME]},
    }, indent=2) + '\n', newline='\n')
    for name in FILES + [ZIP_NAME, 'manifest.json']:
        print(sha(release / name), name)


if __name__ == '__main__':
    main()
