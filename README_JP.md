# Android Walkman(2019年リリース)についてのドキュメント

## はじめに

このガイドは、ADBやfastbootといった、基本的なコマンドを知っているユーザーのみを対象としています。  
Android プラットフォーム ツール も必要です。[こちら](https://mitanyan98.hatenablog.com/entry/2023/04/04/150957)を参考に準備してください。

### <ins>免責事項</ins>

- ~~以下の手順はMacでのみテストしました。~~  
- あなたが何かミスをしてデバイスを壊しても、私は責任を負いません、文句も受け付けません。 
- あなたの隣で技術的な問題を解決することはしません。 
- adbのインストール方法やコマンドの実行方法などの基本的なことは教えません。 

### <ins>Windowsユーザー向け</ins>:

まず、次の手順に従ってください。: 

1. Walkmanの開発者向けオプションで、USB デバッグを有効にする 
2. WalkmanをWindows PCに接続する
3. `adb`コマンドが実行できるか確認し、`adb shell getprop ro.boot.slot_suffix`を実行する
4. 出力された結果から、実行パーティションが「a」か「b」のどちらか確認してメモする
5. [こちら](https://github.com/nxp-imx/mfgtools/releases)から`uuu.exe`をダウンロードする
6. `uuu.exe`を作業用ディレクトリに置き、`uuu`コマンドが動作するかを確認する
7. 上手く動作しない場合、`uuu.exe`を直接ターミナルにドラッグして実行することもできます

`uuu`コマンドは、実行中のパーティションにのみ影響を与えます。

## ブートローダーのアンロック

ブートローダー・アンロック(BLU)を行うことで、全てのデータが削除されますのでご注意ください。  
また、[最新版「4.06」までアップデートしていても「4.04」まで戻ってしまう例](https://github.com/notcbw/2019_android_walkman/issues/1#issuecomment-1721653902)が確認されています。  
アップデーターに問題は起きないため、もう一度最新版までアップデートしてください。  
1. 開発者向けオプションで、「OEMロック解除」と「ADBデバッグ」を有効にします 
2. `adb reboot bootloader`を実行してfastboot モードに入ります
3.  fastboot モード(SONYのロゴが表示された状態)で、次のコマンドを実行します
    - Mac/Linux: `fastboot oem unlock`
    - Windows: `uuu FB: oem unlock`
4. これを実行すると、上手く実行できなかったような挙動をますが、実際にはデバイス内でユーザーデータ パーティションを消去しようとしています
   これには500秒(約8分)ほどかかります
5. アンロックが完了したら、次のコマンドを実行して再起動します
   - Mac/Linux: `fastboot reboot`
   - Windows: `uuu FB: reboot`

### AVBの無効化
この手順はカスタム カーネルを使用するために必要です。  
AVBを無効にするには、次のコマンドを使用して`blank_vbmeta.img`をフラッシュします。 : 
- Mac/Linux: `fastboot --disable-verity --disable-verification flash vbmeta blank_vbmeta.img`
- Windows(_a): `uuu FB: flash vbmeta_a blank_vbmeta.img`
- Windows(_b): `uuu FB: flash vbmeta_b blank_vbmeta.img`

最初に数回bootloopが発生し、その後Android システムの起動に失敗したことをリカバリが通知します。  
ここではfactory resetオプションを選択する必要があります。 ボリュームを使用してカーソルを選択し、電源ボタンを使用して確認します。  
工場出荷状態にリセットすると、OS が正しく起動するはずです。  

## カスタムカーネル

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
