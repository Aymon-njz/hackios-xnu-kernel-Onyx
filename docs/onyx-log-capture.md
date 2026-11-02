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

No native SCSI write commands, new raw-write interfaces, or changes to the
working loader/UEFI are introduced by this patch.
