# Verified RPMh inventory: Onyx

Build 41463163865, run ID `rpmh-41463163865`, native journal sequence 9.
607826 payload bytes, both CRCs verified, no console-wrap/capacity flags.
Raw journal and decoded log: local `out/logs/rpmh-41463163865/`.

At approximately 49 seconds, command DB and RSC inventory completed:

| Resource | Address | ARC levels in index order |
| --- | --- | --- |
| CX | `0x30000` | `0,10,40,80,c0,100,140,180,1a0,1b0` |
| MX | `0x30010` | `0,10,38,40,80,c0,e0,100,140,180,1a0,1b0,1c0` |
| GFX | `0x30050` | `0,38,40,80,c0,e0,100,140,180,190,1a0,1b0` |

All levels above are hexadecimal. These match the recovery kernel's command DB
dump. They are abstract levels, not measured millivolts.

* APPS RSC DRV2 ID `0x30800`: v3.8, supported v3 register layout.
* Solver config `0x4010100`: capability bit 24 clear.
* Parent/child config `0x800c8104`: eight TCSs for DRV2, 16 commands per TCS.

| Active TCS | Control | Status | Command-enable | Wait |
| --- | --- | --- | --- | --- |
| 0 | `0x1010000` | `1` | `1` | `0` |
| 1 | `0x10000` | `1` | `0` | `0` |
| 2 | `0x10000` | `1` | `0` | `0` |

Slot 0 retains a firmware request. Slots 1/2 have no enabled commands at the
observed instant, but this is not an ownership grant. No TCS or voltage writes
were issued in this run. A future transport must serialize ownership, check
for competing requests and preserve firmware state. It must also handle bounded
completion/error paths; reusing these slots blindly is not a regulator driver.

Snapshots continued after probe completion (last saved body includes framebuffer
activity beyond 150 seconds). No panic text was found in the verified capture.
The user's subsequent report of a reboot is not explained by this snapshot;
there is no evidence of an immediate fault at an RPMh probe read. Metal and
GPU execution remained unavailable throughout the run.

## Command-detail follow-up

Build 41472232537 (`340964e`), identity `tcs-command-41472232537`, produced
native snapshot 8: 556100 bytes, CRCs verified, no wrap/capacity flags.
The read-only inventory finished at approximately 49 seconds and logging
continued after 104 seconds.

TCS 0 command 0:

| Field | Readback |
| --- | --- |
| Message ID | `0x10108` |
| Resource | `0x40f08` |
| Data | `7` |
| Command status | `0x10101` |

The recovery command DB names base `0x40f00` as `ldob5`. Stock
`rpmh-regulator.c` defines VRM mode register index 2, with register address
`base + 4 * index`. Therefore this request targets ldob5 mode, not the ARC
CX/MX/GFX level resources. Stock `rpmh-rsc.c` defines command issued as bit 8
and complete as bit 16; both are set here. The enabled bit and trigger remain
set despite this completion evidence. Treating every enabled command as a
currently in-flight request is too coarse, but the result does not authorize
clearing firmware state or establish ownership of slots 1/2. Preserve slot 0.

No voltage votes were sent. Device remains in OrangeFox after restoring the
original Android boot image for journal collection. Local full capture:
`out/logs/tcs-command-41472232537/`.
