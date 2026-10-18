"""Build the optimized simulator and an otherwise identical reference with GCC.

Usage: python build_fast.py
       python build_fast.py --cc gcc --output-dir ../perf/rebuilt
The default flags are the measured settings; CPU-specific flags are optional.
"""
import argparse
import json
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import sysconfig


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--flags', default='-std=gnu17 -O3 -fomit-frame-pointer')
    parser.add_argument('--output-dir', type=Path,
                        help='Default: bin/ for generic, a hill-named subdirectory for specialization')
    parser.add_argument('--python-extension', action='store_true',
                        help='Also build a module for the running Windows CPython with MinGW GCC')
    args = parser.parse_args()
    settings = {}
    fixed_definitions = []
    source = Path(__file__).resolve().parent
    if args.output_dir is None:
        args.output_dir = source / 'bin'
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if platform.system() == 'Windows' else ''
    commands = []
    for name, definitions in [('exmars-fast', []),
                              ('exmars-reference', ['-DEXMARS_REFERENCE'])]:
        command = [args.cc, *shlex.split(args.flags), *fixed_definitions, *definitions,
                   *[str(source / f) for f in ('pmars.c', 'pspace.c', 'sim.c')],
                   '-o', str(output / (name + suffix))]
        subprocess.run(command, check=True)
        commands.append(command)
    if args.python_extension:
        if platform.system() != 'Windows':
            parser.error('--python-extension currently supports Windows CPython / MinGW only')
        library = Path(sys.base_prefix) / 'libs' / (
            f'python{sys.version_info.major}{sys.version_info.minor}.lib')
        if not library.is_file():
            parser.error(f'CPython import library not found: {library}')
        command = [args.cc, *shlex.split(args.flags), *fixed_definitions, '-shared',
                   '-I', sysconfig.get_path('include'),
                   *[str(source / f) for f in ('python_mars.c', 'pmars.c', 'pspace.c', 'sim.c')],
                   str(library), '-o', str(output / ('Mars' + sysconfig.get_config_var('EXT_SUFFIX')))]
        subprocess.run(command, check=True)
        commands.append(command)
        shutil.copy2(source / 'Warrior.py', output / 'Warrior.py')
    compiler = subprocess.run([args.cc, '--version'], capture_output=True,
                              text=True, check=True).stdout
    (output / 'build.json').write_text(json.dumps(
        {'compiler': compiler, 'commands': commands, 'fixed_settings': settings,
         'cli_defaults': {'max_length': 100, 'min_distance': 100}}, indent=2), encoding='utf-8')
    print(f'Built optimized and reference simulators in {output}')


if __name__ == '__main__':
    main()
