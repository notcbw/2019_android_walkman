# Documenting 2019 Android-based Walkmans

## Findings

### Firmware Update File

The first 128 bytes of the firmware update file contains the file magic and the SHA-228 digest. The first byte is the magic "NWWM", the next 56 bytes is the SHA-224 digest stored as ASCII hex digits. The rest is unknown.

The encrypted data is a standard Android OTA update zip file. The transformation scheme is AES/CBC/PKCS5Padding.

The encryption key is stored in plain text at `/vendor/usr/data/icx_nvp.cfg` as a 48 character long ASCII text. The first 32 bytes are the AES key and the next 16 bytes are the initialisation vector. NW-A100 series and NW-ZX500 series has different keys.

### Key Combo to Fastboot Mode

Hold Vol- & FF when powering on.

### NVP

All the configuration, flags, keys, etc. are stored in the nvp as raw fields. `nvp`, `nvpflag`, `nvpinfo`, `nvpnode`, `nvpstr` and `nvptest` in `/vendor/bin` are believed to be debug tools used to manipulate the values in nvp. `nvp` is used to display the binary partition in hex format. `nvpflag` is used to view and write some flags such as destination. `nvpstr` controls some other string variables in nvp. The purposes of the others are unknown.

## Guides

### Bootloader Unlocking

This will erase all user data. In developer options, enable OEM unlocking and ADB debugging. Run `adb reboot bootloader` to enter fastboot mode. In fastboot mode (SONY logo), run `fastboot oem unlock`. After running that, it will appear to be stuck, but the device is actually trying to wipe the userdata partition. It would take around 500 seconds. After that, run `fastboot reboot` to reboot to OS.

### Disabling AVB

This step is required to use custom kernels. To disable the AVB, flash the blank vbmeta file with the following command: `fastboot --disable-verity --disable-verification flash vbmeta blank_vbmeta.img`. It will bootloop first, then it would boot to recovery, saying that it failed to boot the Android system. You need to do a factory reset here. After a factory reset the OS should boot correctly.

### Kernel

The kernel source in this repo was patched with KernelSU support, lower CPU frequency support and a more power-saving cpu frequency governor. Use `walkman.config` in kernel_imx to build the kernel.

### Changing Destination (Removing Volume Cap)

1. Flash the KernelSU enabled boot.img to the device.
2. Enable superuser for shell in KernelSU app.
3. Enable ADB debugging in developer options
4. Connect Walkman to PC through USB. Execute `adb shell` to enter shell.
5. Execute `su -` to gain root privilege.
6. Execute `nvpflag shp 0x00000006 0x00000000` then `nvpflag sid 0x00000000` to switch the destination code to E. (for UAE, SEA, HK, SK and Oceania markets, with high-gain support)
7. Disconnect USB connection. Reboot your Walkman.

### Unpacking Update Files

You need your key string for the device first. Enable adb, then execute `adb shell cat /vendor/usr/data/icx_nvp.cfg`. You can find you key string at the NAS section. Make sure you have java version >1.8 in you path by executing `java -version`. Download the firmware decryptor [HERE](https://github.com/notcbw/2019_android_walkman/releases/download/v0/nwwmdecrypt.jar). Run the decryptor by executing `java -jar nwwmdecrypt.jar -i <input file> -o <output file> -k <key string>` in your terminal/CMD/Powershell.

After decrypting, extract the zip file. Use [payload_dumper](https://github.com/vm03/payload_dumper) to unpack the payload.bin file in the extracted zip file.

## Unpacked Fastboot Firmware

- NW-ZX500 series 4.04 Chinese Version (No Google services, better battery life): [https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing](https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing)
- NW-A100 series 4.06 International: [https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing](https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing)
