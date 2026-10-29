# Onyx stock touch baseline

This file records facts extracted from the attached Redmi Turbo 4 Pro / POCO F7
(`onyx`) while booted in its own OrangeFox recovery.  It is the authority for
the native touch port; do not substitute a touch configuration from another
Xiaomi device.

## Controller and transport

| item | verified value |
| --- | --- |
| controller | Novatek NT38771, PID `0x5998` |
| Linux compatible | `novatek,NVT-ts-spi` |
| serial engine | QUPv3_1 SE4, physical address `0x00a90000` (`spi4.0`) |
| DMA engine | QUPv3_1 GPI at `0x00a00000`, IOMMU-managed by Android |
| controller clock | 19.2 MHz maximum in the live DTB |
| SPI pins | GPIO16 MISO, GPIO17 MOSI, GPIO18 SCLK, GPIO19 chip select |
| interrupt | GPIO78, shared with display TE; do not remux it in XNU |
| panel geometry | 1280 x 2772, 10 contacts |
| stock polling window | page `0x031400`, command offset `0x50` (`0x031450`) |
| stock GPI transaction | 2,306-byte full-duplex transfer; Android identifies a 2,048-byte Xiaomi host-polling frame |
| observed GPI transfer setup | mode 0, 19.2 MHz, four-bit words, packing mode 3, chip select 0, full-duplex command 3 |
| stock report cadence | 120 Hz |

The recovery kernel reports the input device as `NVTCapacitiveTouchScreen`.
It loads `nt38771_touch.ko`, whose compatible string is exactly
`novatek,NVT-ts-spi`.  Its active panel is CSOT and it downloads
`novatek_nt38771_o10u_fw_csot.bin` during startup.

## Porting rules

1. Keep the known-good iOS boot payload as the control image.
2. Do not bit-bang GPIO16--19 and do not claim or reconfigure GPIO78.
3. Do not create an arbitrary SMMU context or GPI owner.  The stock transport
   is the `spi-msm-geni` driver using `msm_gpi`; its channel descriptors and
   IOMMU mapping must be reproduced from that driver.
4. Do not use a 66-byte standard NT36xxx point-buffer read as the primary
   transport.  The live Onyx driver selects page `0x031400`, sends command
   `0x50`, and receives its 2,306-byte host-polling transfer over GPI.
5. After the Onyx host-polling envelope has been decoded, its contact records
   use the stock NT36xxx layout: slot data starts at byte 1, each slot is six
   bytes, status is `byte0 & 7`, and the contact ID is `(byte0 >> 3) - 1`.
   Coordinates are encoded as
   `x = byte1 << 4 | byte3 >> 4` and
   `y = byte2 << 4 | byte3 & 0x0f`.

## Non-transferable reference code

The Haki projects are useful architecture references, but their existing
phone-driver code must not be copied into this port: its touch driver targets
Goodix/FocalTech on a different board, and its Adreno driver targets A660/642L.
The Onyx live DTB identifies its GPU as Adreno825 (`qcom,adreno-gpu-gen8-6-0`),
so graphics requires a separate Gen8 port after touch is stable.

The proprietary modules and firmware pulled from the phone are deliberately
kept under the local ignored `hardware/onyx-stock/` directory, not committed.
