"""Boot U-Boot in QEMU with the JetKVM-like gadget passed through and report what its USB stack can use."""

import errno
import json
import os
import sys
import threading
import time

import pexpect

PROMPT = "=> "
KEYCODES = {c: 4 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")} | {" ": 44, "\n": 40}


def write_report(fd, report, deadline):
    """A gadget report write only completes once the host polls; retry until then."""
    while True:
        try:
            os.write(fd, report)
            return True
        except OSError as e:
            if e.errno not in (errno.EAGAIN, errno.ESHUTDOWN) or time.monotonic() > deadline:
                return False
            time.sleep(0.01)


def type_on_gadget(text, timeout=30):
    deadline = time.monotonic() + timeout
    fd = os.open("/dev/hidg0", os.O_RDWR | os.O_NONBLOCK)
    try:
        for c in text:
            for report in (bytes([0, 0, KEYCODES[c], 0, 0, 0, 0, 0]), bytes(8)):
                if not write_report(fd, report, deadline):
                    return
                time.sleep(0.05)
    finally:
        os.close(fd)


def send_idle_reports(stop):
    """Keep an empty report queued, as a keyboard honouring SET_IDLE would. Linux's HID gadget does not."""
    fd = os.open("/dev/hidg0", os.O_RDWR | os.O_NONBLOCK)
    try:
        while not stop.is_set():
            write_report(fd, bytes(8), 0)
            time.sleep(0.01)
    finally:
        os.close(fd)


class UBoot:
    def __init__(self, u_boot):
        self.qemu = pexpect.spawn(
            "qemu-system-aarch64",
            ["-M", "virt", "-cpu", "cortex-a57", "-m", "512", "-nographic", "-nic", "none",
             "-bios", u_boot,
             "-device", "qemu-xhci,id=xhci",
             "-device", "usb-host,bus=xhci.0,vendorid=0x1d6b,productid=0x0104"],
            encoding="utf-8", timeout=120, logfile=sys.stdout, codec_errors="replace",
        )
        self.qemu.expect("Hit any key to stop autoboot")
        self.qemu.sendline("")
        self.qemu.expect_exact(PROMPT)

    def run(self, command):
        self.qemu.sendline(command)
        self.qemu.expect_exact(PROMPT)
        return self.qemu.before

    def reads_disk(self):
        return "HELLO.TXT" in self.run("fatls usb 0").upper()

    def keyboard_bound(self):
        return "usbkbd" in self.run("coninfo")

    def keyboard_types(self):
        self.run("setenv stdin serial,usbkbd")
        typist = threading.Thread(target=type_on_gadget, args=("echo kbdok\n",))
        typist.start()
        # The pty turns U-Boot's \r\n into \r\r\n.
        typed = self.qemu.expect([r"\nkbdok\r*\n", pexpect.TIMEOUT], timeout=40) == 0
        typist.join()
        if not typed:
            self.qemu.sendcontrol("c")
        self.qemu.expect_exact(PROMPT)
        self.run("setenv stdin serial")
        return typed


def main(u_boot):
    u = UBoot(u_boot)
    result = {}

    # The gadget attaches after U-Boot's boot-time scan, so always rescan.
    u.run("usb reset")
    u.run("usb info")
    result["disk"] = u.reads_disk()
    result["keyboard"] = u.keyboard_bound() and u.keyboard_types()

    stop = threading.Event()
    idle = threading.Thread(target=send_idle_reports, args=(stop,))
    idle.start()
    u.run("usb reset")
    stop.set()
    idle.join()
    result["keyboard_with_idle_reports"] = u.keyboard_bound() and u.keyboard_types()

    result["disk_after_reset"] = u.reads_disk()
    u.run("usb stop")
    u.qemu.terminate(force=True)

    print("RESULT " + json.dumps(result))
    with open(sys.argv[2], "w") as f:
        json.dump(result, f)


if __name__ == "__main__":
    main(sys.argv[1])
