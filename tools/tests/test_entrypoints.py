"""Run real CLI preflight and result handling without contacting the sensor."""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import chicago


class EntryPointTests(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            commands = home / 'bin'
            commands.mkdir()
            # shutil.which must really run, including on machines without fprintd.
            for name in ('fprintd-enroll', 'fprintd-verify', 'journalctl', 'timeout'):
                executable = commands / name
                executable.write_text('#!/usr/bin/env sh\nexit 1\n')
                executable.chmod(0o700)
            account = types.SimpleNamespace(pw_dir=tmp, pw_uid=os.getuid(), pw_gid=os.getgid())
            expected = {
                'label': mode,
                'result': 'enroll-completed' if mode == 'enroll' else 'verify-match',
                'scores': [] if mode == 'enroll' else [93],
            }
            output = io.StringIO()
            with (patch.dict(os.environ, {'PATH': str(commands)}),
                  patch.object(sys, 'argv', ['chicago.py', mode, '--user', 'test-user']),
                  patch.object(chicago.os, 'geteuid', return_value=0),
                  patch.object(chicago.os, 'chown'),
                  patch.object(chicago.pwd, 'getpwnam', return_value=account),
                  patch.object(chicago, 'run_client', return_value=expected) as client,
                  patch('builtins.input', side_effect=['', 'q']),
                  contextlib.redirect_stdout(output)):
                self.assertEqual(chicago.main(), 0)
            client.assert_called_once()
            self.assertEqual(client.call_args.args[0], mode)
            self.assertEqual(client.call_args.args[1], 'test-user')
            self.assertEqual(client.call_args.args[-1], 'right-index-finger')
            result_paths = list((home / '.local/state/gxfp5130-chicago').glob('*/results.json'))
            self.assertEqual(len(result_paths), 1)
            self.assertEqual(json.loads(result_paths[0].read_text()), [expected])
            if mode == 'verify':
                self.assertIn('匹配；分数：93', output.getvalue())

    def test_enroll_preflight_and_result(self):
        self.exercise('enroll')

    def test_verify_preflight_and_score(self):
        self.exercise('verify')


if __name__ == '__main__':
    unittest.main()
