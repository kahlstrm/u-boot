# JetKVM USB tests (fork-only)

The `jetkvm` branch is U-Boot plus fixes that make a stock JetKVM's composite
USB device (keyboard, mice, a vendor HID interface and virtual media) usable from
U-Boot. Each fix lives on its own branch based on upstream `main` for submission;
this branch collects them on top of the release used by the Raspberry Pi 5
installer in `kahlstrm/config`.

## Branches

- `usb-kbd-idle`: accept keyboards that only report key presses (Linux USB
  gadgets such as JetKVM).
- `usb-accessors`: route USB state lookups through `usb_get_device()`.
- `usb-composite` (on `usb-accessors`): bind a driver to each interface of a
  composite device, set up xHCI endpoints for every interface, and keep
  per-function state on each function's device.
- `jetkvm`: the above on the release, plus these tests.
- `jetkvm-rpi5`: `jetkvm` plus the Raspberry Pi 5 RP1/NVMe support used by the
  Talos installer.

## Tests

A NixOS test VM builds a JetKVM-like gadget with Linux's `dummy_hcd` and
configfs, then boots U-Boot (`qemu_arm64_defconfig`) in QEMU with an xHCI
controller and the gadget passed through. No host root is needed.

```bash
nix flake check -L                                    # every variant and layout
nix build -L .#checks.x86_64-linux.branch-stock       # one combination
```

- Variants: `upstream` (the release this branch is based on) and `branch` (this tree).
- Controllers: `xhci` (`qemu-xhci`) and `ehci` (`usb-ehci`).
- Layouts: `stock` (JetKVM order, storage last), `storage-first` (kahlstrm/kvm),
  and `stock-altsetting` (with a function that has alternate settings before the
  storage, as with JetKVM's optional audio; NCM stands in, since `dummy_hcd`
  cannot provide audio's isochronous endpoints).
- [expected.json](expected.json) records what each combination can use: read the
  FAT disk, type on the keyboard, the keyboard with periodic idle reports (which
  Linux's HID gadget does not send), and the disk after `usb reset`. Update it in
  the same commit as a fix that changes a result. `null` leaves a result
  unchecked; upstream's EHCI keyboard after `usb reset` with alternate settings
  varies between runs, because upstream EHCI leaks interrupt queues on timeout.

## Testing on a Raspberry Pi 5 with a JetKVM (`jetkvm-rpi5`)

`nix build .#rpi5-launcher` builds a 65 MiB disk image with this U-Boot and the
Pi 5 boot files of the pinned `sbc-raspberrypi` overlay (the `config.txt` is the
one the Talos installer writes). Its partition is plain FAT32, not an EFI
system partition, so U-Boot ignores it and boots the node's NVMe as usual. The
node's disks are not written.

It needs a Pi 5 whose EEPROM tries USB before NVMe (`BOOT_ORDER=0xf164`):

1. Upload the image to the JetKVM as virtual media in disk mode (not CD-ROM)
   and mount it.
2. Power-cycle the Pi and watch its UART. U-Boot should report the JetKVM's
   storage device during `usb start`.
3. Interrupt autoboot from the JetKVM keyboard, then run `usb tree` and
   `fatls usb 0` there; the listing shows `u-boot.bin` and `config.txt`.
4. Run `boot` (or power-cycle and let autoboot run); the node boots from NVMe.
5. Unmount the image; the next power cycle boots the node's own U-Boot.
