# JetKVM USB tests (fork-only)

The `jetkvm` branch is U-Boot plus fixes that make a stock JetKVM's composite
USB device (keyboard, mice, a vendor HID interface and virtual media) usable from
U-Boot. Each fix lives on its own branch based on upstream `main` for submission;
this branch collects them on top of the release used by the Raspberry Pi 5
installer in `kahlstrm/config`.

## Tests

A NixOS test VM builds a JetKVM-like gadget with Linux's `dummy_hcd` and
configfs, then boots U-Boot (`qemu_arm64_defconfig`) in QEMU with an xHCI
controller and the gadget passed through. No host root is needed.

```bash
nix flake check -L                                    # every variant and layout
nix build -L .#checks.x86_64-linux.branch-stock       # one combination
```

- Variants: `upstream` (the release this branch is based on) and `branch` (this tree).
- Layouts: `stock` (JetKVM order, storage last) and `storage-first` (kahlstrm/kvm).
- [expected.json](expected.json) records what each combination can use: read the
  FAT disk, type on the keyboard, the keyboard with periodic idle reports (which
  Linux's HID gadget does not send), and the disk after `usb reset`. Update it in
  the same commit as a fix that changes a result.
