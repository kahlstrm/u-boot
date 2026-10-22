# Creates a JetKVM-like composite USB gadget on dummy_hcd with interfaces in argument order.
# Usage: jetkvm-gadget <function>... (keyboard mouse_abs mouse_rel wake storage)
# HID functions are always created in the same order, so /dev/hidg0 is the keyboard.

gadget=/sys/kernel/config/usb_gadget/jetkvm
disk=/var/lib/jetkvm-disk.img

hex() { printf '%b' "$(sed 's/ //g; s/\(..\)/\\x\1/g' <<<"$1")"; }

hid() {
  local name=$1 subclass=$2 protocol=$3 length=$4 descriptor=$5
  mkdir "$gadget/functions/hid.$name"
  echo "$subclass" >"$gadget/functions/hid.$name/subclass"
  echo "$protocol" >"$gadget/functions/hid.$name/protocol"
  echo "$length" >"$gadget/functions/hid.$name/report_length"
  hex "$descriptor" >"$gadget/functions/hid.$name/report_desc"
}

truncate -s 32M "$disk"
mkfs.vfat -n JETKVM "$disk" >/dev/null
echo "hello from the gadget" >/tmp/hello.txt
mcopy -i "$disk" /tmp/hello.txt ::/HELLO.TXT

mkdir "$gadget"
echo 0x1d6b >"$gadget/idVendor"
echo 0x0104 >"$gadget/idProduct"
mkdir "$gadget/strings/0x409" "$gadget/configs/c.1"
echo JetKVM >"$gadget/strings/0x409/product"

hid keyboard 1 1 8 "05010906a101050719e029e71500250175019508810295017508810395057501050819012905910295017503910395067508150025650507190029658100c0"
hid mouse_abs 0 0 5 "05 01 09 02 a1 01 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 03 05 01 09 30 09 31 15 00 26 ff 7f 75 10 95 02 81 02 c0 c0"
hid mouse_rel 1 2 3 "05010902a1010901a100050919012903150025019503750181029501750581030501093009311581257f750895028106c0c0"
hid wake 0 0 1 "06 00 ff 09 01 a1 01 15 00 26 ff 00 75 08 95 01 09 01 81 02 c0"
mkdir "$gadget/functions/mass_storage.storage"
echo 1 >"$gadget/functions/mass_storage.storage/lun.0/removable"
echo 0 >"$gadget/functions/mass_storage.storage/lun.0/cdrom"
echo "$disk" >"$gadget/functions/mass_storage.storage/lun.0/file"

for function in "$@"; do
  case $function in
    storage) ln -s "$gadget/functions/mass_storage.storage" "$gadget/configs/c.1/" ;;
    *) ln -s "$gadget/functions/hid.$function" "$gadget/configs/c.1/" ;;
  esac
done

ls /sys/class/udc >"$gadget/UDC"
