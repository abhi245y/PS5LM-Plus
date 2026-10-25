#!/usr/bin/env python3
"""Exercise the real logger against a full pipe on a POSIX development host.

Requires a C++17 compiler and initialized llama.cpp submodule. No PS5 SDK needed.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
LLAMA = ROOT / 'third_party/llama.cpp'
PATCH = ROOT / 'patches/0006-log-never-blocks.patch'


def run(args, **kwargs):
    return subprocess.run(args, check=True, timeout=120, **kwargs)


def main():
    run(['git', '-C', str(LLAMA), 'apply', '--reverse', '--check', str(PATCH)])
    with tempfile.TemporaryDirectory(prefix='ps5lm-log-test-') as tmp:
        tmp = Path(tmp)
        original = tmp / 'original'
        original.mkdir()
        (original / 'log.cpp').write_bytes(subprocess.check_output(
            ['git', '-C', str(LLAMA), 'show', 'HEAD:common/log.cpp']))
        # Simulate a writer that has not yet been scheduled. This deterministically
        # exercises a full queue at pause(), without changing production sources.
        delayed = tmp / 'delayed'
        delayed.mkdir()
        source = (LLAMA / 'common/log.cpp').read_text()
        start = 'thrd = std::thread([this]() {'
        assert source.count(start) == 1
        source = source.replace(start, start + '\nstd::this_thread::sleep_for(std::chrono::milliseconds(500));')
        (delayed / 'log.cpp').write_text(source)
        unsafe = tmp / 'unsafe-stop'
        unsafe.mkdir()
        stop_wait = 'cv_full.wait(lock, [this]() { return !is_full(); });'
        assert source.count(stop_wait) == 1
        (unsafe / 'log.cpp').write_text(source.replace(stop_wait, '/* negative control: no stop-slot reservation */'))
        policies = [('ps5', True, LLAMA / 'common'),
                    ('native', False, LLAMA / 'common'),
                    ('original-ps5', True, original),
                    ('delayed-ps5', True, delayed),
                    ('unsafe-stop-ps5', True, unsafe)]
        for policy, ps5, directory in policies:
            exe = tmp / policy
            command = shlex.split(os.environ.get('CXX', 'c++'))
            command += ['-std=c++17', '-pthread']
            if ps5:
                command += ['-D__PROSPERO__']
            for include in [directory, LLAMA / 'common', LLAMA / 'include',
                            LLAMA / 'ggml/include', LLAMA / 'vendor']:
                command += ['-I' + str(include)]
            command += [str(ROOT / 'tests/log-backpressure.cpp'),
                        str(LLAMA / 'common/json.cpp'), '-o', str(exe)]
            run(command)
            for mode in [[], ['json']]:
                args = [str(exe), *mode, *(['repeat'] if policy == 'ps5' else [])]
                try:
                    result = subprocess.run(args, capture_output=True, text=True,
                                            timeout=3 if policy == 'unsafe-stop-ps5' else 30)
                except subprocess.TimeoutExpired:
                    if policy != 'unsafe-stop-ps5':
                        raise
                    print(f'{policy} {mode or ["text"]}: PASS (shutdown hang reproduced)')
                    continue
                if policy == 'unsafe-stop-ps5':
                    raise RuntimeError('Unsafe stop-marker control did not reproduce the hang')
                expected = 1 if policy == 'original-ps5' else 0
                if result.returncode != expected:
                    raise RuntimeError(f'{policy} {mode}: {result.stdout} {result.stderr}')
                lines = result.stdout.splitlines()
                label = 'original policy (expected blocking)' if expected else policy
                print(f'{label}: {lines[-1]} ({len(lines)} iteration(s))')
    print('All logger checks passed; producer blocking and unsafe shutdown controls reproduced.')


if __name__ == '__main__':
    main()
