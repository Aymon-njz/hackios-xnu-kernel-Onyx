# First verified live Onyx GPU capture

Build `41443545834`, commit `f81d3c91d9ca3b7f117c713f3308f5988608f0ce`,
run identity `native-41443545834`.

Recovered from OrangeFox using the pinned userdata journal. The native reader
validated both CRC32 values, identity and length. Sequence 6 contains 367675
bytes; flags are 1 (committed, no reported wrap/capacity loss). Snapshots 1..5
report status zero and readback verification. This validates live persistence
on this phone for this run; instant crashes can still lose the latest messages.

GPUCC reads all completed without a reset during the probe:

| Register | Value |
| --- | --- |
| CXO AON CBCR `0x9008` | `0` |
| CX GDSCR `0x9080` | `0x22f801` |
| CX HW control `0x9094` | `0x2800010a` |
| AHB CBCR `0x90bc` | `0` |
| CX GMU CBCR `0x90d4` | `0x80002001` |
| CXO CBCR `0x90e4` | `0x80000001` |
| HUB CX INT CBCR `0x90e8` | `0x80000001` |
| GMU RCG command/config `0x9318/0x931c` | `0x80000000 / 0x100000` |
| HUB AON CBCR `0x93e8` | `0` |
| HUB RCG command/config `0x93ec/0x93f0` | `0 / 0x100000` |

Stock `drivers/clk/qcom/gdsc.c` defines power-on status as bit 31 and software
collapse as bit 0. CX GDSCR has power-on clear and collapse set. Stock
`clk-branch.h` defines CBCR clock-off as bit 31; the GMU, CXO and HUB CX INT
values have that bit set despite bit-0 enable votes. This is evidence the
GPU power/clock sequence remains necessary, not evidence of a working GMU.
Do not treat raw enable bits alone as running clocks.

Recovery's clk_summary and pm_genpd_summary reads produced no reference data.
No GMU, 3D, GPU SMMU or power-control writes were performed by this test.
No Metal acceleration is available. The test's automatic return did not expose
USB; the user manually entered stock fastboot, then the saved Android boot was
restored for OrangeFox. The live capture survived that path.

Raw journal and decoded log are local at
`out/logs/native-41443545834/{journal.bin,kernel.log}`. They are not vendor
firmware and are retained locally rather than committed as full device logs.
