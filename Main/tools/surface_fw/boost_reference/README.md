# Surface SAM VBST reference

The three entries in `register_entries.json` were checked against the bytes of
SurfaceSAM 9.101.139 variant 0 (`surface/analysis/sam/body_0.bin`). The SHA-256 of
that body and both extraction JSON files are recorded in `manifest.json`.
Offsets are zero-based offsets into the body, whose mapped base is `0x80000`.
Normal verification only reads these project-local excerpts.

The initialization entries are index 55 in table `0x841EC` and index 54 in table
`0x8442C`. Each wire packet contains a big-endian 32-bit address and 32-bit value:
`00 00 38 00 00 00 00 AA`.

The WSEQ excerpt comes from block `wseq_001_028017c0.bin`, source pointer
`0x8BD08`, at payload offset 96. Its eight bytes are
`00 38 00 00 00 00 00 AA`: two padded 24-bit DSP words encode a 16-bit register
address and a 32-bit value. The four-byte bus destination prefix in the
extraction's `wire_hex` is not part of the body payload or the offset.

Cirrus Logic's public `cs40l2x_boost_config()` provides the interpretation:

```text
code = (voltage_mV - 2550) / 50 + 1     (2550..11000 mV)
0xAA = 170 -> 2550 + (170 - 1) * 50 = 11000 mV
```

[Public driver and encoding source](https://android.googlesource.com/kernel/google-modules/amplifiers/+/refs/heads/android-gs-raviole-6.1-android16/cs40l25/cs40l2x.c)
(Git blob recorded in the manifest).

This parameter is applied to the project's existing `0x0A0603` firmware without
changing the image. External boost remains enabled and MP28167 VREF remains
652 (521.6 mV; nominal rail 13.19648 V with the project's 243k/10k divider).
The SAM WSEQ also contains `0x3804=0x101`; that control selection is **not**
imported by this change. Amplifier gain, internal boost enable and waveforms
are unchanged. Register verification is not a measurement of VBST, actuator
peak voltage, RMS voltage or Vpp. Board measurement remains pending.
