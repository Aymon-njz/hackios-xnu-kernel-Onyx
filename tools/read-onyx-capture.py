#!/usr/bin/env python3
"""Extract an identified, returned kernel capture; refuse a stale journal tail."""
import argparse
import re
import sys
import struct
import zlib
from pathlib import Path

SEP = b'\n== KERNEL LOG ==\n'
END = b'\n== END OF LOG ==\n'


def extract(raw, run_id):
    if raw.startswith(b'ONYXLOG1'):
        return extract_native(raw, run_id)
    # Legacy loader writes are padded, and old captures remain beyond them.
    # Require current-run identity AND return metadata in the leading header.
    # A NUL in the kernel body is data, not an end-of-capture indicator.
    sep = raw.find(SEP)
    padding = raw.find(b'\0' * 16)
    if sep < 0 or (padding >= 0 and padding < sep):
        raise ValueError('no returned kernel capture in the current journal header')
    header = raw[:sep]
    command = re.search(rb'(?:^|\n)cmdline: ([^\r\n]*)', header)
    if not command or run_id.encode() not in command.group(1).split():
        raise ValueError('journal command line does not identify the requested run')
    returned = re.search(rb'kernel returned: log at [0-9a-fA-Fx]+, ([0-9]+) bytes;', header)
    if not returned:
        raise ValueError('no kernel-return length in the current header')
    size = int(returned.group(1))
    start = sep + len(SEP)
    if size > len(raw) - start or raw[start + size:start + size + len(END)] != END:
        raise ValueError('capture is truncated or its returned length/end marker disagree')
    return raw[start:start + size]


def extract_native(raw, run_id):
    if len(raw) < 4096:
        raise ValueError('truncated native header')
    header = bytearray(raw[:4096])
    version, header_bytes = struct.unpack_from('<II', header, 8)
    sequence, size = struct.unpack_from('<QQ', header, 16)
    crc, flags = struct.unpack_from('<II', header, 32)
    expected_header_crc, = struct.unpack_from('<I', header, 168)
    header[168:172] = b'\0' * 4
    if zlib.crc32(header) != expected_header_crc:
        raise ValueError('native header CRC mismatch')
    if version != 1 or header_bytes != 4096 or not sequence or not flags & 1 or flags & ~7:
        raise ValueError('unsupported or incomplete native header')
    identity = bytes(header[40:168]).split(b'\0', 1)[0]
    if not run_id.startswith('onyx_log_id=') or identity != run_id.split('=', 1)[1].encode():
        raise ValueError('native capture identity does not match the requested run')
    if not size or size > 2 * 1024 * 1024 - 4096 or size > len(raw) - 4096:
        raise ValueError('native capture length is invalid or truncated')
    body = raw[4096:4096 + size]
    if zlib.crc32(body) != crc:
        raise ValueError('native payload CRC mismatch (interrupted update or corrupt data)')
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    parser.add_argument('--run-id', required=True, help='exact unique boot argument, e.g. onyx_gpu_run=capture-123')
    args = parser.parse_args()
    try:
        data = extract(args.journal.read_bytes(), args.run_id)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Capture unavailable: {error}\n')
    sys.stdout.buffer.write(data)


if __name__ == '__main__':
    main()
