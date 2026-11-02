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

## Diagnostic build 41485393676

Recovered `gmu-trace-41485393676` from recovery. Both CRCs and run ID
validated: snapshot 7, 499306 payload bytes, no wrap/capacity flags. The helper
reached phase 3: offset **0x9008**, mask 1, expected 1, observed **0** before
rollback. Both HUB and GMU root config/update checks therefore completed before
this branch vote failed. This rules out root config rejection as the failure in
this run. It does not prove every branch is running or that GMU access is ready.

The stock Tuna GPUCC probe sets bit 0 at 0x9008 (`gpu_cc_cxo_aon_clk`)
among its always-on setup, after PLL configuration. It does not define that
register as a normal polled `clk_branch`. Our generic branch treatment needs
review against that ordering and the XO reset state; removing readback checks
would not establish a working clock. No firmware boot was attempted. Logging
continued after the failed test. Recovery devmem cannot read the registers
because /dev/mem is absent; no recovery register writes were made.

## Always-on sequence patch

The clock helper now writes all five stock always-on prerequisite controls in
probe order (0x93a4, 0x9008, 0x93a8, 0x9004, 0x90cc), preserving other bits,
before configuring TCXO roots and normal branches. These are not generic
clk_branch objects, so undefined bit31 halt polling is removed for 0x9008.
After the full sequence, all five bit0 requests must read back as enabled;
phase 5 reports a mismatch and rolls back/quarantines. Normal branches retain
strict vote and documented halt/delay checks. Reverse rollback now covers the
always-on requests too. PLL and reset programming remain untouched.

This addresses an incomplete/misordered prerequisite sequence, not a proven
physical cause of the non-latching 0x9008 vote. It cannot establish GMU readiness
without a new hardware capture. Tests include stock write order, rollback of
all prerequisites and persistent 0x9008 rejection (still refused, not ignored).
