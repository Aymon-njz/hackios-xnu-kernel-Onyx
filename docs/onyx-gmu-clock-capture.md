# GMU clock capture, 2026-11-02

Kernel build 41482125558, commit ab3e9d0, stage 6.
Recovered from OrangeFox after restoring the verified Android boot image.
`out/logs/gmu-clock-41482125558/journal.bin` passed both CRCs and exact
run identity checks: native snapshot 28, 1114561 payload bytes, no wrapping or
capacity flag. Successful snapshot messages continue through 704 seconds.

CX/MX RPMh votes returned status zero. CX GDSCR changed from 0x22f801 to
0xf822f800. The GMU/HUB helper returned ONYX_CLOCK_READBACK (3), quarantined
further attempts, and attempted rollback. Both root configurations subsequently
read 0x100000. This does not identify the failing write: those reads occur
AFTER rollback. There is no evidence of a root timeout or GPU firmware start.

The next diagnostic captures the precise phase, offset, mask, expected value and
raw observed value BEFORE rollback. It preserves the same hardware sequence
and timeout limits. Tests assert that config rejection, root timeout and branch
timeout diagnostics survive rollback. Do not skip the failed check or claim
working clocks, GMU, GPU execution or Metal from this capture.
