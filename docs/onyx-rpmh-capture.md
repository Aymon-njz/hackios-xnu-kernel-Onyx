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
