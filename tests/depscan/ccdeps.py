# SPDX-License-Identifier: GPL-3.0-only
"""Record the files one compiler call reads (tests/run_suite.py --changed; host only).

    python3 tests/depscan/ccdeps.py REAL_COMPILER ARGS...

Called by the tests/host_cc wrapper before the real compile when BR_HOST_DEPS_LOG names a file:
runs REAL_COMPILER with the same flags and sources under -M -MG (preprocess only, every header
listed, missing generated headers tolerated) and appends each file to the log as an absolute
path. Best effort: a failure here never changes the real compile, which runs next either way."""
import os
import subprocess
import sys

SOURCE = ('.c', '.cc', '.cpp', '.cxx', '.m', '.S', '.s')
DROP_WITH_ARG = {'-o', '-MF', '-MT', '-MQ', '-L', '-l', '-framework', '-Xlinker', '-arch'}


def main(argv):
    log = os.environ.get('BR_HOST_DEPS_LOG')
    if not log or len(argv) < 2:
        return
    real, args = argv[0], argv[1:]
    keep, sources, i = [], [], 0
    while i < len(args):
        a = args[i]
        if a in DROP_WITH_ARG:
            i += 2
            continue
        if a in ('-c', '-M', '-MM', '-MD', '-MMD', '-MP', '-E', '-S', '-fsyntax-only', '-shared', '-v') or \
                a.startswith(('-Wl,', '-l', '-fsanitize')):
            i += 1
            continue
        if not a.startswith('-') and a.endswith(SOURCE):
            sources.append(a)
        elif not a.startswith('-') and a.endswith(('.o', '.a', '.dylib', '.so')):
            i += 1
            continue
        keep.append(a)
        i += 1
    if not sources:
        return
    try:
        r = subprocess.run([real, *keep, '-M', '-MG', '-w'], capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.SubprocessError):
        return
    files = set(os.path.abspath(s) for s in sources)
    for tok in r.stdout.replace('\\\n', ' ').split():
        if tok.endswith(':') or tok == '\\':
            continue
        files.add(os.path.abspath(tok))
    with open(log, 'a') as f:
        f.write(''.join(p + '\n' for p in sorted(files)))


if __name__ == '__main__':
    main(sys.argv[1:])
