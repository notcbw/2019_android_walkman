# Android Walkman(2019年リリース)についてのドキュメント

## はじめに

このガイドは、ADBやfastbootといった、基本的なコマンドを知っているユーザーのみを対象としています。  
Android プラットフォーム ツール も必要です。[こちら](https://mitanyan98.hatenablog.com/entry/2023/04/04/150957)を参考に準備してください。

### <ins>免責事項</ins>

- ~~以下の手順はMacでのみテストしました。~~  
- あなたが何かミスをしてデバイスを壊しても、私は責任を負いません、文句も受け付けません。 
- あなたの隣で技術的な問題を解決することはしません。 
- adbのインストール方法やコマンドの実行方法などの基本的なことは教えません。 

### <ins>For Windows Users Only</ins>:

Please do the following steps first:

1. Enable USB Debugging in Developer options on your Walkman
2. Connect your Walkman to your Windows PC.
3. Make sure you can use `adb`. Execute `adb shell getprop ro.boot.slot_suffix`
4. Take note of the output. It should be either "_a" or "_b".
5. Download uuu [HERE](https://github.com/nxp-imx/mfgtools/releases/download/uuu_1.5.21/uuu.exe)
6. Put `uuu.exe` in your working directory. Try executing `uuu` to see if it works.

The output of the command will be the suffix of the partition when executing `uuu` commands.

## Bootloader Unlocking

This will erase all user data. 
1. In developer options, enable OEM unlocking and ADB debugging.
2. Run `adb reboot bootloader` to enter fastboot mode.
3. In fastboot mode (SONY logo), run:
    - Mac/Linux: `fastboot oem unlock`
    - Windows: `uuu FB: oem unlock`
4. After running that, it will appear to be stuck, but the device is actually trying to wipe the userdata partition. It would take around 500 seconds.
5. After the process has finished, run the following command to reboot.
   - Mac/Linux: `fastboot reboot`
   - Windows: `uuu FB: reboot`

### Disabling AVB

This step is required to use custom kernels. To disable the AVB, flash the blank vbmeta file with the following command: 
- Mac/Linux: `fastboot --disable-verity --disable-verification flash vbmeta blank_vbmeta.img`
- Windows(_a): `uuu FB: flash vbmeta_a blank_vbmeta.img`
- Windows(_b): `uuu FB: flash vbmeta_b blank_vbmeta.img`

It will bootloop first, then it would boot to recovery, saying that it failed to boot the Android system. You need to choose the factory reset option here. Use volume to control the cursor and the power button to confirm. After a factory reset the OS should boot correctly.

## Kernel

The kernel source in this repo was patched with KernelSU support, lower CPU frequency support and a more power-saving cpu frequency governor. Use the `walkman.config` file provided as the config.

My prebuilt one is [HERE](https://github.com/notcbw/2019_android_walkman/releases/tag/v1). To flash it, enter fastboot, then execute: (If you have a a100, change the file name)

- Mac/Linux: `fastboot flash boot boot-zx500.img`.
- Windows(_a): `uuu FB: flash boot_a boot-zx500.img`
- Windows(_b): `uuu FB: flash boot_b boot-zx500.img`

### Changing Destination (Removing Volume Cap for ZX500, not tested and probably have no effect for A100)

1. Do everything mentioned above (bootloader unlocking, disabling avb, then flash the provided kernel)
2. Download and install KernelSU app from [HERE](https://github.com/tiann/KernelSU/releases/download/v0.6.7/KernelSU_v0.6.7_11210-release.apk)
3. Open KernelSU app. Enable superuser for shell in KernelSU app.
4. Enable ADB debugging in developer options
5. Connect Walkman to PC through USB. Execute `adb shell` to enter shell.
6. Execute `su -` to gain root privilege.
7. Execute `nvpflag shp 0x00000006 0x00000000` then `nvpflag sid 0x00000000` to switch the destination code to E. (for UAE, SEA, HK, SK and Oceania markets, with high-gain support)
8. Disconnect USB connection. Reboot your Walkman. The high-gain option should be available.

## Findings

## Unpacking Update Files

You need your key string for the device first. Enable adb, then execute `adb shell cat /vendor/usr/data/icx_nvp.cfg`. You can find you key string at the NAS section. Make sure you have java version >1.8 in you path by executing `java -version`. Download the firmware decryptor [HERE](https://github.com/notcbw/2019_android_walkman/releases/download/v0/nwwmdecrypt.jar). Run the decryptor by executing `java -jar nwwmdecrypt.jar -i <input file> -o <output file> -k <key string>` in your terminal/CMD/Powershell.

After decrypting, extract the zip file. Use [payload_dumper](https://github.com/vm03/payload_dumper) to unpack the payload.bin file in the extracted zip file.

## Unpacked Fastboot Firmware

- NW-ZX500 series 4.06 International: [https://drive.google.com/file/d/1TUFwOOrex2miPd41UAhe8ioKbxIv4M0R/view?usp=sharing](https://drive.google.com/file/d/1TUFwOOrex2miPd41UAhe8ioKbxIv4M0R/view?usp=sharing)
- NW-ZX500 series 4.04 Chinese Version (No Google services, better battery life): [https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing](https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing)
- NW-A100 series 4.06 International: [https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing](https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing)

### Firmware Update File

The first 128 bytes of the firmware update file contains the file magic and the SHA-228 digest. The first byte is the magic "NWWM", the next 56 bytes is the SHA-224 digest stored as ASCII hex digits. The rest is unknown.

The encrypted data is a standard Android OTA update zip file. The transformation scheme is AES/CBC/PKCS5Padding.

The encryption key is stored in plain text at `/vendor/usr/data/icx_nvp.cfg` as a 48 character long ASCII text. The first 32 bytes are the AES key and the next 16 bytes are the initialisation vector. NW-A100 series and NW-ZX500 series has different keys.

### Key Combo to Fastboot Mode

Hold Vol- & FF when powering on.

### NVP

All the configuration, flags, keys, etc. are stored in the nvp as raw fields. `nvp`, `nvpflag`, `nvpinfo`, `nvpnode`, `nvpstr` and `nvptest` in `/vendor/bin` are believed to be debug tools used to manipulate the values in nvp. `nvp` is used to display the binary partition in hex format. `nvpflag` is used to view and write some flags such as destination. `nvpstr` controls some other string variables in nvp. The purposes of the others are unknown.
