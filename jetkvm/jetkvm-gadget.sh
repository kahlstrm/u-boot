# Creates a composite USB gadget matching a JetKVM on dummy_hcd with interfaces in argument order.
# Usage: jetkvm-gadget <function>... (keyboard mouse_abs mouse_rel wake altsetting storage)
# HID functions are always created in the same order, so /dev/hidg0 is the keyboard.

gadget=/sys/kernel/config/usb_gadget/jetkvm
disk=/var/lib/jetkvm-disk.img

hex() { printf '%b' "$(sed 's/ //g; s/\(..\)/\\x\1/g' <<<"$1")"; }

# A function whose data interface has alternate settings, like JetKVM's
# optional audio. Audio itself needs isochronous endpoints, which dummy_hcd
# cannot provide; NCM's data interface has an empty setting 0 and a bulk
# setting 1.
altsetting() {
  mkdir "$gadget/functions/ncm.altsetting"
  ln -s "$gadget/functions/ncm.altsetting" "$gadget/configs/c.1/"
}

hid() {
  local name=$1 subclass=$2 protocol=$3 length=$4 no_out=$5 descriptor=$6
  mkdir "$gadget/functions/hid.$name"
  echo "$subclass" >"$gadget/functions/hid.$name/subclass"
  echo "$protocol" >"$gadget/functions/hid.$name/protocol"
  echo "$length" >"$gadget/functions/hid.$name/report_length"
  echo "$no_out" >"$gadget/functions/hid.$name/no_out_endpoint"
  hex "$descriptor" >"$gadget/functions/hid.$name/report_desc"
}

truncate -s 32M "$disk"
mkfs.vfat -n JETKVM "$disk" >/dev/null
echo "hello from the gadget" >/tmp/hello.txt
mcopy -i "$disk" /tmp/hello.txt ::/HELLO.TXT

# Attributes and report descriptors match jetkvm/kvm internal/usbgadget.
mkdir "$gadget"
echo 0x0200 >"$gadget/bcdUSB"
echo 0x1d6b >"$gadget/idVendor"
echo 0x0104 >"$gadget/idProduct"
echo 0x0100 >"$gadget/bcdDevice"
mkdir "$gadget/strings/0x409" "$gadget/configs/c.1" "$gadget/configs/c.1/strings/0x409"
echo JetKVM >"$gadget/strings/0x409/manufacturer"
echo "JetKVM USB Emulation Device" >"$gadget/strings/0x409/product"
echo "Config 1: HID" >"$gadget/configs/c.1/strings/0x409/configuration"
echo 250 >"$gadget/configs/c.1/MaxPower"
echo 0xa0 >"$gadget/configs/c.1/bmAttributes"

hid keyboard 1 1 8 0 05010906a101050719e029e71500250175019508810295017508810395057501050819012905910295017503910395067508150025ff0507190029ff8100c0
hid mouse_abs 0 2 6 1 05010902a10185010901a1000509190129051500250175019505810295017503810305010930093116000026ff7f36000046ff7f751095028102c0850209381581257f35004500750895018106050c0a38021581257f750895018106c0
hid mouse_rel 1 2 5 1 05010902a1010901a1000509190129081500250195087501810205010930093109381581257f750895038106050c0a38021581257f750895018106c0c0
hid wake 0 0 1 1 0600ff0901a101150026ff007508950109018102c0
mkdir "$gadget/functions/mass_storage.storage"
echo 0 >"$gadget/functions/mass_storage.storage/stall"
echo 1 >"$gadget/functions/mass_storage.storage/lun.0/removable"
echo 0 >"$gadget/functions/mass_storage.storage/lun.0/cdrom"
echo "JetKVM  Virtual Media" >"$gadget/functions/mass_storage.storage/lun.0/inquiry_string"
echo "$disk" >"$gadget/functions/mass_storage.storage/lun.0/file"

for function in "$@"; do
  case $function in
    storage) ln -s "$gadget/functions/mass_storage.storage" "$gadget/configs/c.1/" ;;
    altsetting) altsetting ;;
    *) ln -s "$gadget/functions/hid.$function" "$gadget/configs/c.1/" ;;
  esac
done

ls /sys/class/udc >"$gadget/UDC"
