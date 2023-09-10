# Documenting 2019 Android-based Walkmans

## Findings

### Firmware Update File

The first 128 bytes of the firmware update file contains the file magic and the SHA-228 digest. The first byte is the magic "NWWM", the next 56 bytes is the SHA-224 digest stored as ASCII hex digits. The rest is unknown.

The encrypted data is a standard Android OTA update zip file. The transformation scheme is AES/CBC/PKCS5Padding.

The encryption key is stored in plain text at `/vendor/usr/data/icx_nvp.cfg` as a 48 character long ASCII text. The first 32 bytes are the AES key and the next 16 bytes are the initialisation vector. NW-A100 series and NW-ZX500 series has different keys.

### Bootloader Unlocking

It supports bootloader unlocking. You can access the fastboot mode by either ADB or holding vol-, vol+, next and play/pause button at startup. After executing `fastboot oem unlock`, it appears to be stuck, but it's actually trying to wipe the userdata partition of the device. It would take around 500 seconds. 

### Disabling AVB

To disable the AVB, find an empty vbmeta partition then flash it with the following command: `fastboot --disable-verity --disable-verification flash vbmeta <empty vbmeta file>`. It will bootloop first, then it would boot to recovery, saying that it failed to boot the Android system. You need to do a factory reset here. After a factory reset everything should be fine.

### Kernel

The kernel source is at [https://prodgpl.blob.core.windows.net/download/Audio/20211022/gpl_source.tgz](https://prodgpl.blob.core.windows.net/download/Audio/20211022/gpl_source.tgz). Use android\_dmp1\_defconfig. You can build it after a few modifications with the official ARM GNU Toolchain. Clang/LLVM might not work so well.

## Unpacked Fastboot Firmware

- NW-ZX500 4.04 Chinese Version (No Google services, better battery life): [https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing](https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing)
