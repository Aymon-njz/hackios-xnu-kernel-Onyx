# Onyx kernel capture

The known boot container writes its loader journal into the last 2 MiB of
userdata. Native UFS mode 4 runs without a working live USB/mailbox channel;
kernel logs must return to the resident loader before that loader can persist
them. A forced reset is not a kernel log export. An initial loader header alone
does not prove that the kernel's log was saved.

## Return path change

`loader_return.c` previously required the current user TTBR0 address size to be
25..33 and silently returned otherwise. The kernel now installs a private 4 KiB,
39-bit TTBR0 walk for its return stub, preserving TTBR1's kernel configuration.
The private walk tables are cleaned to RAM before switching. A diagnostic reports
the original/new TCR and stub address. This removes a concrete unsupported return
condition; the prior device's runtime TCR was not captured, so this is not proof
that address-size refusal caused every missing log.

The capture test uses a unique `onyx_gpu_run=` argument and
`loader_return_after=90`. Returning after 90 seconds is intentional. It should
stop iOS, return to the loader and persist its kernel log. This remains a
hardware test; neither host tests nor compilation prove that the loader can
resume and write on this phone.

## Recovery collection

OrangeFox requires the saved Android `boot_a` on this device. Verify the serial,
active slot and saved image hash before restoring it for recovery. Collect the
journal read-only after checking userdata's current size and block size. The
previously verified geometry was 249517027328 bytes, 4096-byte blocks, with the
journal at block 60916731 for 512 blocks. Do not treat these numbers as a generic
layout for another device.

After saving the complete 2 MiB journal locally:

```
python3 tools/read-onyx-capture.py journal.bin \
  --run-id onyx_gpu_run=THE_EXACT_TEST_ID > kernel.log
```

The reader requires the unique argument in the current loader header, a
`kernel returned:` length, a kernel-log separator and a matching end marker.
Embedded NULs in the kernel body are preserved. A padded initial header followed
by old data is refused. "Capture unavailable" is a failed capture, not an empty
kernel log or proof that the GPU probe never ran. Keep the raw journal for audit.

## Live native snapshots (new path, requires hardware validation)

The timed return test still reset to stock fastboot without persisting a kernel
log. The alternative `onyx_log_native=1 onyx_log_id=UNIQUE_ID` writes while XNU
runs, without returning to UEFI. It introduces an internal native WRITE(10)
exception; ordinary storage slots/devices still refuse writes.

Recovery verified userdata starts at sector 24997888 (512-byte units), or LUN-0
4096-byte LBA 3124736, and is 249517027328 bytes long. The only permitted write
range is absolute LUN-0 LBA **64041467 through 64041978**, the same final 2 MiB
journal already used by the loader. Any change to its start, size, block size,
LUN, native/GPT mapping or existing journal signature disables this writer.
No new writable device or ioctl is exposed. Per-slot checks require the exact
command/tag/LBA/count and FUA flag before placement, after placement, and before
ringing. Each chunk is read back and compared. A storage error stops logging.

The first snapshot is attempted five seconds after native UFS becomes ready,
then every ten seconds for six captures and every thirty seconds thereafter.
It captures the existing loader-console head/tail ring and the kernel message
buffer; it does not capture every process's private/unified logs. Infinite log
history cannot fit into 2 MiB. The header flags report when console history has
wrapped or snapshot capacity was reached. This is not a panic handler and an
instant reset can lose messages since the last successful snapshot.

The on-disk `ONYXLOG1` header is 4096 bytes: version/header length at 8/12,
sequence/payload length at 16/24, payload CRC32/flags at 32/36, a 128-byte
NUL-terminated run identity at 40, header CRC32 at 168 (calculated with this
field zero), and kernel uptime ticks at 176. Integer fields are little-endian.
Flag 1 marks a committed capture, flag 2 means the console ring wrapped, flag 4
means capacity was reached. The payload follows. Body chunks are written and
verified before the header is committed. A reset during an update can invalidate
the previous capture; the CRC checks must reject it. This is a single bounded
snapshot slot, not an atomic or unlimited logging filesystem.

Read the complete journal from recovery as above, then:

```
python3 tools/read-onyx-capture.py journal.bin --run-id onyx_log_id=UNIQUE_ID > kernel.log
```

The native reader validates both CRCs, the bounds and the exact identity. No
changes to the working loader/UEFI or general filesystem write access are made.
