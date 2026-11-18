# SPDX-License-Identifier: GPL-3.0-only
"""Record the files a test runner's Python processes read (tests/run_suite.py --changed).

tests/run_suite.py puts this directory first on PYTHONPATH, with BR_HOST_DEPS_LOG naming a log
file, only while it records the dependency map: every Python process the runner starts then
appends the files and directories it opens, lists or copies (an audit hook, PEP 578). It never
changes what a runner does."""
import os
import sys

_log = os.environ.get('BR_HOST_DEPS_LOG')
if _log:
    _seen = set()
    _events = {'open': 0, 'os.listdir': 0, 'os.scandir': 0, 'shutil.copyfile': 0, 'shutil.copytree': 0,
               'os.link': 0, 'os.symlink': 0, 'glob.glob': 0}

    def _hook(event, args, _log=_log, _seen=_seen):
        if event not in _events or not args:
            return
        path = args[0]
        if isinstance(path, int) or path is None:
            return
        try:
            path = os.fsdecode(path)
            if event == 'glob.glob':
                path = os.path.dirname(path) or '.'
            path = os.path.abspath(path)
        except Exception:
            return
        if path in _seen:
            return
        if event in ('os.listdir', 'os.scandir') and any(path == os.path.abspath(p or '.') for p in sys.path):
            return                                 # the import system scanning sys.path
        _seen.add(path)
        try:
            fd = os.open(_log, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o644)
            try:
                os.write(fd, (('D ' if event in ('os.listdir', 'os.scandir', 'shutil.copytree', 'glob.glob')
                               else 'F ') + path + '\n').encode())
            finally:
                os.close(fd)
        except Exception:
            pass

    _seen.add(os.path.abspath(_log))
    sys.addaudithook(_hook)
