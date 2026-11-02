#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('capture', Path(__file__).with_name('read-onyx-capture.py'))
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTest(unittest.TestCase):
    def make_capture(self, body):
        return (b'cmdline: -v onyx_gpu_run=test\njumping\n'
                + f'kernel returned: log at 8c001000, {len(body)} bytes; reason 0\n'.encode()
                + capture.SEP + body + capture.END)

    def test_embedded_nuls_are_preserved(self):
        body = b'first\0second' + b'\0' * 64 + b'last'
        self.assertEqual(capture.extract(self.make_capture(body), 'onyx_gpu_run=test'), body)

    def test_old_tail_is_refused(self):
        raw = b'cmdline: onyx_gpu_run=test\njumping\n' + b'\0' * 32 + self.make_capture(b'old')
        with self.assertRaises(ValueError): capture.extract(raw, 'onyx_gpu_run=test')

    def test_wrong_run_and_partial_argument_are_refused(self):
        for identity in ['onyx_gpu_run=tes', 'onyx_gpu_run=other']:
            with self.assertRaises(ValueError): capture.extract(self.make_capture(b'log'), identity)

    def test_incomplete_and_bad_length_are_refused(self):
        raw = self.make_capture(b'log')
        for changed in [raw[:-1], raw.replace(b'3 bytes;', b'300 bytes;')]:
            with self.assertRaises(ValueError): capture.extract(changed, 'onyx_gpu_run=test')


unittest.main()
