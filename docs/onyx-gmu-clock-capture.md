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

## Stock-driver comparison after HUB failure

Recovery on the same phone accepted `clk_prepare_enable=1` for
`gpu_cc_hub_aon_clk`, while `clk_print_regs` still reported CBCR 0. The logical
request count read 1; it was returned to 0 afterward. Raw output is retained in
`out/logs/stock-hub-clock/enable-test.txt`. This experiment does not measure a
clock waveform or prove a running GMU.

The kernel's stock `clk_branch2_check_halt` accepts CLK_OFF clear OR NOC FSM
status 2. It does not require bit0 readback after the write. The XNU helper now
uses that bounded status condition for the polled Tuna branches and reports
non-echoing bit0 requests in separate masks (branch order CXO,HUB_AON,HUB_CX_INT,
AHB,CX_GMU; always-on order CB,CXO_AON,RSCC_HUB_AON,RSCC_XO_AON,SLEEP).
AHB retains its stricter request check because it has only a delay contract.
Always-on controls are written as in stock and non-echoing values are logged,
not classified as a halt failure. Root config checks, bounded waits,
quarantine and rollback remain. No firmware boot is enabled by this change.
A zero register can satisfy stock status semantics; this is explicitly not
independent proof of physical clock activity or accessibility of GMU registers.

Tests cover a non-echoing HUB request with CLK_OFF clear, a rejected request
with CLK_OFF set (must time out), FSM ON despite CLK_OFF, a rejected AHB request
(must fail), and non-echoing always-on requests with diagnostic masks.

## AHB stock-driver comparison

Run gmu-stock-41492866975 passed journal CRC/identity checks: snapshot 8,
545790 bytes. It reached the AHB bit0 readback check, which returned zero.
Stock recovery subsequently accepted AHB clk_prepare_enable=1 and reported
logical count 1 while its CBCR still read zero; the logical request was then
returned to zero. Saved output: out/logs/stock-ahb-clock/enable-test.txt.

AHB now follows its stock BRANCH_HALT_DELAY contract: enable request, 10 us
settle, no unsupported bit0-echo requirement. Non-echoing requests remain in
the diagnostic mask. This does NOT independently validate physical AHB/GMU
access, and no GMU firmware start is enabled by this patch. The always-on HUB
logical request from the earlier stock comparison may remain physically set
until reboot because its stock ops omit disable. No raw MMIO writes were made
in recovery; debugfs was unmounted after the comparison.

## Verified AHB-fix hardware capture

Run gmu-ahb-41495189072 validated both journal CRCs, exact identity and length:
snapshot 11, 724611 bytes, flags 1 (no wrap or capacity truncation reported).
Both RPMh votes returned 0; CX changed from 0x22f801 to 0xf822f800.
The clock helper returned 0 with quarantine 0. GMU and HUB commands read 2,
configs 0x100001; CX GMU branch read 0x2001, CXO and HUB CX INT read 1.
Non-echo masks were branches 0xa and always-on 0x2, matching the measured
stock treatment of HUB AON, AHB and CXO AON. The log continued with framebuffer
updates past 221 seconds. No GMU firmware was started and no GPU work ran.
Raw evidence stays local at out/logs/gmu-ahb-41495189072/.

The restored Android boot partition was SHA256-verified against the saved
backup after a successful sparse restore in 8 MiB transfers. Large 96 MiB
raw transfers had disconnected; the sparse image expands byte-for-byte to
the backup, including its AVB footer.

Stage 7 (onyx_gpu_stage=7 onyx_gpu_power=1 onyx_gmu_access=1) adds a read-only
GMUCX AO-counter sample after successful clock setup. Recovery FDT maps GMU
at 0x03d37000/0x68000. Pinned stock graphics source
8500748dae32f10507c02f1c94bc971553cb525b/gen8_reg.h and gen8_read_alwayson
use GPU-relative word indices 0x1f840/0x1f841: physical 0x03d7e100/104.
High/low/high reads retry at most eight times; samples are 1 ms apart.
A pre-access message gets a 35-second persistence window, then power and
clock status is rechecked. Counter advancement is evidence of accessible
AO state, not of a running CM3, firmware, GPU job, or Metal implementation.
This new stage is not yet hardware tested.

## Verified GMUCX counter access

Run gmu-counter-41891315686 passed exact run identity and both CRC checks:
snapshot 22, 1032974 bytes, flags 1. Power/clock setup again returned success
without quarantine. At 84.142 seconds, the GMUCX AO sample returned 0:
0x604910b0 -> 0x60496700, delta 22096 across the requested 1 ms delay
(which includes scheduling/MMIO overhead). Logs and framebuffer updates
continued beyond 553 seconds. This establishes readable, advancing GMUCX AO
state on the tested device; it does not establish CM3 firmware execution.
Evidence: out/logs/gmu-counter-41891315686/{journal.bin,kernel.log,result.json}.
The Android boot partition was again hash-verified after recovery restore.

The current parser also accepts the phone's saved gen80600_gmu.bin: 15
blocks. Its executable code includes a CACHE payload at GMU VA 0x4000 of
0x15868 bytes, plus fixed ITCM and DTCM payloads. Metadata reserves further
CACHE regions at 0x100000, 0x200000 and 0x300000, and uncached pages at
0x60000000/0x60001000. Consequently firmware startup requires real GMU IOMMU
backing, not only TCM copies. That backing, HFI shared-memory mapping and
stock secure CX initialization remain prerequisites before releasing CM3.
No firmware upload, CM3 reset release or GPU job was attempted in this run.

## Private GMU translation tables

The new onyx_gmu_mmu builder creates unattached AArch64 stage-1 LPAE tables:
32-bit IOVAs, conservative 40-bit PAs, 4 KiB L1/L2/L3 translations. Firmware
CACHE pages can be read/execute and normal-WB; HFI data can be read/write,
non-executable and normal-noncacheable. MAIR slots must match 0x44/0xff.
Writable executable mappings are refused. Mapping preflights every page and
table capacity before changing descriptors, so overlaps, exhausted arenas,
and corrupt child descriptors leave the requested range unpublished.
Tests cover actual firmware/HFI address ranges, both table boundaries,
read-only/XN/cache attributes, overflow and atomic failure behavior.

This is CPU-built table infrastructure, not a live SMMU domain. No TTBR,
SCTLR, stream match, context bank or TLB registers are written. Before attach:
allocate and retain physical backing for every firmware reservation and HFI
queue; verify the GMU stream 5 routing and a private context bank from live
SMMU state; establish TCR/MAIR attributes from the hardware capabilities;
flush data/tables; then install and invalidate with bounded status checks.
Do not reuse the existing A6xx stream/context assignments for Onyx.

## Live attachment prerequisite

Recovery currently exposes the GPU SMMU but no attached GMU domain or IOMMU
group. Its GMU device has no bound driver. This does not prove that a context
bank is free of firmware or hypervisor ownership. No recovery SMMU registers
were changed; debugfs was unmounted after inspecting its available interfaces.

Stage 8 adds read-only global SMMU inventory after successful GMUCX counter
access, gated by the GPUCC HLOS1 SMMU clock-off bit. It validates stream-match
support and register-window geometry, then reports every SMR/S2CR matching
GMU SID 5. It never reads context-bank pages or writes SMR, S2CR, TTBR or
SCTLR. Unknown geometry or an off clock stops the test. The pre-access log
gets a persistence window. This is not attachment: live route/context
ownership still must be established before installing the private tables.

## Firmware/shared-memory preparation component

onyx_gmu_memory_prepare now accepts an allocator/cache-publication interface
and a fresh private page-table arena. It parses the full firmware first,
allocates page-rounded backing for external reservations, rejects overlapping
physical allocations (including the translation-table arena), copies external
payloads, prepares the HFI queues at 0x60002000, and publishes buffers/tables
through the caller's DMA-maintenance callback. TCM remains a separate upload
step; no CM3 reset or MMIO writes are performed. Every failure releases
allocations and discards the private root table. It must not be called on an
attached/live domain; teardown requires the hardware to be quiesced first.

Host tests pass with a synthetic stream and the saved gen80600_gmu.bin,
including translation checks, exact firmware copies and failure injection
at every allocation. These callbacks are not yet wired to XNU's physical
allocator or a live SMMU bank. Cached firmware mappings currently use RX/WB
policy; full firmware data permissions require stock-attribute review before
firmware execution. HFI uses RW/NX/noncacheable memory. No successful firmware
boot, HFI exchange, GPU command/fence or Gen8 Metal shader execution is claimed.

## SMMU inventory test result and clock prerequisite

Run gmu-smmu-41897639553 passed identity and CRC checks: snapshot 13,
771549 bytes, no wrap/truncation flags. GMUCX AO again advanced. The SMMU
inventory was deliberately skipped because GPUCC 0x7000 read 0x80000000
(HLOS1 SMMU clock off); no SMMU register access or context attachment occurred.

The new opt-in onyx_smmu_clock=1 follows stock Tuna's voted branch at GPUCC
0x7000: request bit0, bounded clock-off/FSM polling, restore the prior vote
on timeout and quarantine further access. It requires established CX power.
Stage 8 preserves its pre-vote log, then checks this result before inventory.
Host tests cover the enable path, timeout and rollback. Hardware validation
is still required. Stream routing, context attachment and GPU firmware boot
are not inferred from these software tests.

## Verified GPU SMMU clock and global access

Run gmu-smmu-clock-41906562290 validated identity and both CRCs: snapshot 10,
648155 bytes, no wrap/capacity flags. At 119 seconds the HLOS1 vote returned
success with GPUCC 0x7000=1. At 154 seconds global reads returned IDR0
0x4c017e18, IDR1 0x40000016, IDR2 0x5511: 22 banks, 24 match slots and a
4 KiB register page. No active SMR matched GMU stream 5. iOS/framebuffer logs
continued past 191 seconds. No domain was attached or firmware started.

The next inventory records all 24 raw SMR/S2CR slots, including invalid
slots, and GR1 CBAR/CBA2R metadata for all 22 banks. It does not read or
write bank-local TTBR/SCTLR registers. Missing SID5 routing alone does not
prove that an arbitrarily selected slot or context belongs to this port.
