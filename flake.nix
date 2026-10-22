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
      };

      expected = lib.importJSON ./jetkvm/expected.json;

      check =
        variant: layout:
        pkgs.testers.runNixOSTest {
          name = "u-boot-usb-${variant}-${layout}";
          nodes.machine = {
            virtualisation.memorySize = 2048;
            virtualisation.cores = 2;
            boot.kernelModules = [
              "libcomposite"
              "dummy_hcd"
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
            machine.succeed("uboot-usb-check ${variants.${variant}}/u-boot.bin /tmp/result.json >&2")
            result = json.loads(machine.succeed("cat /tmp/result.json"))
            expected = json.loads('${builtins.toJSON expected.${variant}.${layout}}')
            assert result == expected, f"expected {expected}, got {result}"
          '';
        };
    in
    {
      packages.${system} = lib.mapAttrs' (n: v: lib.nameValuePair "u-boot-${n}" v) variants;
      checks.${system} = lib.listToAttrs (
        lib.concatMap (
          variant:
          map (layout: lib.nameValuePair "${variant}-${layout}" (check variant layout)) (lib.attrNames layouts)
        ) (lib.attrNames variants)
      );
    };
}
