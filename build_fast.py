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
    parser.add_argument('--fixed-coresize', type=int, default=0,
                        help='Fix core size; 0 keeps it dynamic (default)')
    parser.add_argument('--fixed-processes', type=int, default=0,
                        help='Fix process limit; 0 keeps it dynamic (default)')
    parser.add_argument('--fixed-cycles', type=int, default=0,
                        help='Fix cycles per warrior; 0 keeps it dynamic (default)')
    parser.add_argument('--max-length', type=int, help='Default CLI warrior length limit')
    parser.add_argument('--min-distance', type=int, help='Default CLI minimum separation')
    args = parser.parse_args()
    for name, limit in [('fixed_coresize', 65536), ('fixed_processes', 2147483647),
                        ('fixed_cycles', 2147483647)]:
        if not 0 <= getattr(args, name) <= limit:
            parser.error(f'--{name.replace("_", "-")} must be 0..{limit}')
    for name in ('max_length', 'min_distance'):
        if getattr(args, name) is not None and getattr(args, name) <= 0:
            parser.error(f'--{name.replace("_", "-")} must be positive')
    settings = dict(coresize=args.fixed_coresize, processes=args.fixed_processes,
                    cycles=args.fixed_cycles)
    fixed_definitions = [f'-DEXMARS_FIXED_{name.upper()}={value}'
                         for name, value in settings.items() if value]
    if args.max_length is not None:
        fixed_definitions.append(f'-DEXMARS_DEFAULT_LENGTH={args.max_length}')
    if args.min_distance is not None:
        fixed_definitions.append(f'-DEXMARS_DEFAULT_MINSEP={args.min_distance}')
    source = Path(__file__).resolve().parent
    if args.output_dir is None:
        args.output_dir = source / 'bin'
        if any(settings.values()) or args.max_length is not None or args.min_distance is not None:
            tag = (f'hill-c{args.fixed_coresize}-p{args.fixed_processes}-t{args.fixed_cycles}'
                   f'-l{args.max_length or 100}-d{args.min_distance or 100}')
            args.output_dir /= tag
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
         'cli_defaults': {'max_length': args.max_length, 'min_distance': args.min_distance}}, indent=2), encoding='utf-8')
    print(f'Built optimized and reference simulators in {output}')


if __name__ == '__main__':
    main()
