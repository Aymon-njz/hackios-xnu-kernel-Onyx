#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
import struct
import zlib

spec = importlib.util.spec_from_file_location('capture', Path(__file__).with_name('read-onyx-capture.py'))
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTest(unittest.TestCase):
    def make_native(self, body):
        header = bytearray(4096)
        header[:8] = b'ONYXLOG1'
        struct.pack_into('<IIQQII', header, 8, 1, 4096, 1, len(body), zlib.crc32(body), 1)
        header[40:44] = b'test'
        struct.pack_into('<I', header, 168, zlib.crc32(header))
        return bytes(header) + body

    def test_native_preserves_binary_and_rejects_corruption(self):
        body = b'kernel\0GPUCC=1234\n'
        raw = self.make_native(body)
        self.assertEqual(capture.extract(raw, 'onyx_log_id=test'), body)
        for changed in [raw[:-1], raw[:-1] + b'X', raw[:50] + b'X' + raw[51:]]:
            with self.assertRaises(ValueError): capture.extract(changed, 'onyx_log_id=test')
        with self.assertRaises(ValueError): capture.extract(raw, 'onyx_log_id=other')
        with self.assertRaises(ValueError): capture.extract(raw, 'onyx_gpu_run=test')

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
