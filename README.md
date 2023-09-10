# Documenting 2019 Android-based Walkmans

## Findings

### Firmware Update File

The first 128 bytes of the firmware update file contains the file magic and the SHA-228 digest. The first byte is the magic "NWWM", the next 56 bytes is the SHA-224 digest stored as ASCII hex digits. The rest is unknown.

The encrypted data is a standard Android OTA update zip file. The transformation scheme is AES/CBC/PKCS5Padding.

The encryption key is stored in plain text at `/vendor/usr/data/icx_nvp.cfg` as a 48 character long ASCII text. The first 32 bytes are the AES key and the next 16 bytes are the initialisation vector. NW-A100 series and NW-ZX500 series has different keys.

### Key Combo to Fastboot Mode

Hold Vol- & FF when powering on.

## Guides

### Bootloader Unlocking

This will erase all user data. In developer options, enable OEM unlocking and ADB debugging. Run `adb reboot bootloader` to enter fastboot mode. In fastboot mode (SONY logo), run `fastboot oem unlock`. After running that, it will appear to be stuck, but the device is actually trying to wipe the userdata partition. It would take around 500 seconds. After that, run `fastboot reboot` to reboot to OS.

### Disabling AVB

This step is required to use custom kernels. To disable the AVB, flash the blank vbmeta file with the following command: `fastboot --disable-verity --disable-verification flash vbmeta blank_vbmeta.img`. It will bootloop first, then it would boot to recovery, saying that it failed to boot the Android system. You need to do a factory reset here. After a factory reset the OS should boot correctly.

### Kernel

The kernel source is at [https://prodgpl.blob.core.windows.net/download/Audio/20211022/gpl_source.tgz](https://prodgpl.blob.core.windows.net/download/Audio/20211022/gpl_source.tgz). Use android\_dmp1\_defconfig. You can build it after a few modifications with the official ARM GNU Toolchain. Clang/LLVM might not work so well.

## Unpacked Fastboot Firmware

- NW-ZX500 series 4.04 Chinese Version (No Google services, better battery life): [https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing](https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing)
- NW-A100 series 4.06 International: [https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing](https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing)
