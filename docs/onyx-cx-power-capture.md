# First successful Onyx CX power-on

User flashed `onyx-cx-power-41478424310-boot.img`, let iOS run and manually
entered fastboot. Original Android boot was restored for OrangeFox collection.

Native capture identity `cx-power-41478424310`, sequence 9, 640344 bytes.
Both header/payload CRCs and identity verified; no wrap/capacity flags.
Raw journal/decoded log: local `out/logs/cx-power-41478424310/`.

At approximately 48.94 seconds:

| Step | Observed result |
| --- | --- |
| CX ARC vote at level `0x1b0` | transport status 0 |
| MX ARC vote at level `0x1c0` | transport status 0 |
| CX GDSCR before | `0x22f801` |
| CX enable result | 0 |
| CX GDSCR after | `0xf822f800` |

GDSCR power-on bit 31 changed from 0 to 1; software-collapse bit 0 changed
from 1 to 0. This is real-device evidence that the experimental DRV2 transport
and CX power-on operation completed in this run. It does not establish general
regulator/DVFS support or a firmware ownership protocol for other boot modes.
The completed unrelated firmware request was not overwritten by the transport.

The driver deliberately stopped before GMU/GX access. Snapshots continued at
54, 64, 74, 104 and 134 seconds; the final capture contains later activity.
No GMU firmware, GPU command or Metal execution was attempted. Highest-level
diagnostic rail votes were left in place until the user rebooted. The phone is
now running recovery using the saved Android boot image, not this power-test
kernel.

Next required hardware milestone: GMU/HUB clock setup and gated register
access, followed by authenticated firmware load, memory mappings, HFI startup
and GPU command execution. A powered CX domain is not a functioning GPU.
