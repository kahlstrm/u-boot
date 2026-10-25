# Fork-only: tests this tree's USB host stack against a JetKVM-like composite gadget in QEMU.
# See jetkvm/README.md.
{
  description = "U-Boot USB tests against JetKVM-like composite gadgets";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      lib = pkgs.lib;

      uboot =
        version: src:
        pkgs.pkgsCross.aarch64-multiplatform.buildUBoot {
          inherit version src;
          defconfig = "qemu_arm64_defconfig";
          extraConfig = ''
            CONFIG_USB_XHCI_HCD=y
            CONFIG_USB_XHCI_PCI=y
            CONFIG_USB_STORAGE=y
            CONFIG_USB_KEYBOARD=y
            CONFIG_CMD_USB=y
            CONFIG_CMD_FAT=y
          '';
          extraMeta.platforms = [ "aarch64-linux" ];
          filesToInstall = [ "u-boot.bin" ];
        };

      variants = {
        upstream = uboot "2026.10" (
          pkgs.fetchFromGitHub {
            owner = "u-boot";
            repo = "u-boot";
            tag = "v2026.10";
            hash = "sha256-BXIpyAY+uKSohrdG4BxAo0zq4CYmh2z9rFw1jA7Yu8s=";
          }
        );
        branch = uboot "2026.10-jetkvm" (
          lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.difference ./. (
              lib.fileset.unions [
                ./flake.nix
                ./flake.lock
                ./jetkvm
                ./.github
              ]
            );
          }
        );
      };

      # Interface orders: stock JetKVM puts mass storage last; kahlstrm/kvm puts it first.
      layouts = {
        stock = [
          "keyboard"
          "mouse_abs"
          "mouse_rel"
          "wake"
          "storage"
        ];
        storage-first = [
          "storage"
          "keyboard"
          "mouse_abs"
          "mouse_rel"
          "wake"
        ];
        # With a function that has alternate settings before the storage, as
        # with JetKVM's optional audio.
        stock-altsetting = [
          "keyboard"
          "mouse_abs"
          "mouse_rel"
          "wake"
          "altsetting"
          "storage"
        ];
      };

      controllers = {
        xhci = "qemu-xhci";
        ehci = "usb-ehci";
      };

      expected = lib.importJSON ./jetkvm/expected.json;

      # Raspberry Pi 5: U-Boot from this tree and the boot files of the pinned
      # sbc-raspberrypi overlay used by the Talos installer.
      rpi5UBoot = pkgs.pkgsCross.aarch64-multiplatform.buildUBoot {
        version = "2026.10-jetkvm";
        src = variants.branch.src;
        defconfig = "rpi_arm64_defconfig";
        # Talos writes its boot entries through efivarfs during installs and upgrades.
        extraConfig = ''
          CONFIG_EFI_RT_VOLATILE_STORE=y
        '';
        extraMeta.platforms = [ "aarch64-linux" ];
        filesToInstall = [ "u-boot.bin" ];
      };

      rpi5BootFiles =
        pkgs.runCommand "rpi5-boot-files"
          {
            nativeBuildInputs = [ pkgs.jq ];
            image = pkgs.dockerTools.pullImage {
              imageName = "ghcr.io/siderolabs/sbc-raspberrypi";
              imageDigest = "sha256:af82d9f97241fb66c5b660b03e9154fd54ba4560b58ed3b53765bb930d564167";
              hash = "sha256-5IZnFjVm48b3Ep9d6urv1NbuefGukPikzYz26i7CVwM=";
              finalImageTag = "v0.2.2";
              os = "linux";
              arch = "arm64";
            };
          }
          ''
            mkdir archive root
            tar -xf "$image" -C archive
            for layer in $(jq -r '.[0].Layers[]' archive/manifest.json); do
              tar -xf "archive/$layer" -C root
            done
            cp -r root/artifacts/rpi_5 "$out"
          '';

      # A JetKVM virtual disk that starts this U-Boot on a Pi 5 whose EEPROM
      # boots USB first. The partition is not an EFI system partition, so
      # U-Boot boots the NVMe as usual and nothing on the node is written.
      rpi5Launcher =
        pkgs.runCommand "rpi5-launcher.img"
          {
            nativeBuildInputs = with pkgs; [
              dosfstools
              mtools
              util-linux
            ];
          }
          ''
            truncate -s 64M part.img
            # A fixed volume ID and timestamps keep the image reproducible.
            mkfs.vfat -F 32 -n LAUNCHER -i 4c41554e --invariant part.img
            mcopy -s -i part.img ${rpi5BootFiles}/* ::/
            mcopy -i part.img ${./jetkvm/rpi5/config.txt} ::/config.txt
            mcopy -i part.img ${rpi5UBoot}/u-boot.bin ::/u-boot.bin
            truncate -s 65M "$out"
            printf 'label: dos\nlabel-id: 0x4c41554e\nstart=2048, type=c\n' | sfdisk --quiet "$out"
            dd if=part.img of="$out" bs=1M seek=1 conv=notrunc status=none
          '';

      check =
        variant: controller: layout:
        pkgs.testers.runNixOSTest {
          name = "u-boot-usb-${variant}-${controller}-${layout}";
          nodes.machine = {
            virtualisation.memorySize = 2048;
            virtualisation.cores = 2;
            boot.kernelModules = [
              "libcomposite"
              "dummy_hcd"
              "usb_f_ncm"
            ];
            environment.systemPackages = [
              pkgs.qemu
              (pkgs.writers.writePython3Bin "uboot-usb-check" {
                libraries = [ pkgs.python3Packages.pexpect ];
                flakeIgnore = [ "E501" ];
              } (builtins.readFile ./jetkvm/uboot_usb_check.py))
              (pkgs.writeShellApplication {
                name = "jetkvm-gadget";
                runtimeInputs = [
                  pkgs.coreutils
                  pkgs.dosfstools
                  pkgs.mtools
                ];
                text = builtins.readFile ./jetkvm/jetkvm-gadget.sh;
              })
            ];
          };
          testScript = ''
            import json
            machine.wait_for_unit("multi-user.target")
            machine.succeed("jetkvm-gadget ${lib.concatStringsSep " " layouts.${layout}}")
            machine.succeed("uboot-usb-check ${variants.${variant}}/u-boot.bin ${
              controllers.${controller}
            } /tmp/result.json >&2")
            result = json.loads(machine.succeed("cat /tmp/result.json"))
            expected = json.loads('${builtins.toJSON (expected.${variant}.${controller}.${layout} or null)}')
            assert expected is not None, f"no expectation recorded, got {result}"
            # null marks a result that is not checked (see jetkvm/README.md)
            assert result.keys() == expected.keys(), f"expected {expected}, got {result}"
            mismatches = {k: v for k, v in result.items() if expected[k] is not None and expected[k] != v}
            assert not mismatches, f"expected {expected}, got {result}"
          '';
        };
    in
    {
      packages.${system} = lib.mapAttrs' (n: v: lib.nameValuePair "u-boot-${n}" v) variants // {
        rpi5-u-boot = rpi5UBoot;
        rpi5-launcher = rpi5Launcher;
      };
      checks.${system} = lib.listToAttrs (
        lib.concatMap (
          variant:
          lib.concatMap (
            controller:
            map (
              layout: lib.nameValuePair "${variant}-${controller}-${layout}" (check variant controller layout)
            ) (lib.attrNames layouts)
          ) (lib.attrNames controllers)
        ) (lib.attrNames variants)
      );
    };
}
