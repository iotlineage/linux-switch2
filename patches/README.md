# Additive live LTK activation (experimental)

`linux-7.3-switch2-additive-ltk.patch` changes Bluetooth **core**, not the Joy-Con
HID driver or `/dev/switch2-gatt` ABI. Base: CachyOS/linux commit
`6f1d90319ee9a72139f60d4547c2b44064ee2987` (tag `cachyos-7.3.0-4`).
Test build uses the installed **7.3.0-5-cachyos** headers and Module.symvers.
The source tag and installed package revision differ; this is explicit and the
resulting module still requires physical loading/compatibility validation.
Do not apply this patch blindly to another kernel.

The trusted management command 0x0100 takes 39 bytes: a 16-byte UUID/version,
operation 0 (probe) or 1 (add), six-byte Bluetooth little-endian public address,
and 16-byte little-endian LTK. UUID/version bytes are
`85 19 a2 14 69 2b 43 c5 b8 72 7e 06 58 e2 10 01`.
A successful reply contains exactly that UUID/version. This is a project ABI,
not an allocated upstream opcode; clients must reject mismatched replies.
It supports only unauthenticated legacy central public-address LTKs with
EncSize=16, EDiv=0, Rand=0, as required by the measured Joy-Con 2 protocol.

The operation does not overwrite existing keys or issue HCI commands. Identical
keys are idempotent; different existing peer keys return AlreadyPaired. A new
key with a target LE connection returns Busy, so the provider disconnects only
that target before retrying. All unrelated connections and keys remain intact.
Add/remove/clear/SMP identity and key-deletion writers share an LTK lock. New
nodes are fully initialized before RCU publication. LOAD_LONG_TERM_KEYS refuses
while LE/ACL connections exist because full replacement can invalidate SMP-held
key pointers (see BlueZ fork's API documentation for research reference).

Build externally against exact target headers:

```sh
git apply /path/to/linux-7.3-switch2-additive-ltk.patch
make -C /usr/lib/modules/7.3.0-5-cachyos/build M="$PWD/net/bluetooth" LLVM=1 modules
python /path/to/linux-switch2/tests/test-additive-ltk.py "$PWD"
```

The test compiles the actual additive helper into a fake list/lock environment
and checks allocation failure, busy target, conflict refusal, idempotency and
preservation of 2000 concurrently added unrelated keys. It does not exercise
real kernel RCU, SMP, module loading or hardware. BlueZ tests separately exercise
durable storage, private D-Bus, crash boundaries and recovery.

ns-os packages bluetooth.ko under an exact-kernel depmod override, keeping the
distribution's original module. Initial installation needs a stack reload or
reboot; **individual enrollments need neither**. Keep associations on rollback.
Do not silently port or rebuild for a new kernel without repeating validation.
