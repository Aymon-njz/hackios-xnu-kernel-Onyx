#!/usr/bin/env python3
"""Extract an identified, returned kernel capture; refuse a stale journal tail."""
import argparse
import re
import sys
from pathlib import Path

SEP = b'\n== KERNEL LOG ==\n'
END = b'\n== END OF LOG ==\n'


def extract(raw, run_id):
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
