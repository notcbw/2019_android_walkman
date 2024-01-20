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
1. 開発者向けオプションで、「OEMロック解除」と「ADBデバッグ」を有効にする 
2. `adb reboot bootloader`を実行してfastboot モードに入る
3.  fastboot モード(SONYのロゴが表示された状態)で、次のコマンドを実行する
    - Mac/Linux: `fastboot oem unlock`
    - Windows: `uuu FB: oem unlock`
4. これを実行すると、上手く実行できなかったような挙動をしますが、実際にはデバイス内でユーザーデータ パーティションを消去しようとしています
   これには500秒(約8分)ほどかかります
5. アンロックが完了したら、次のコマンドを実行して再起動する
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

このリポジトリのカーネルソースには、[KernelSU](https://kernelsu.org/)のサポート、より低いCPU周波数と省電力なガバナーのパッチを適用しています。  
configには提供されている`walkman.config`ファイルを使ってください。

ビルド済みのカーネルは[こちら](https://github.com/notcbw/2019_android_walkman/releases/)。これをフラッシュするには、fastbootモードに入り、以下のコマンドを実行します。

- Mac/Linux: `fastboot flash boot boot-zx500.img`.
- Windows(_a): `uuu FB: flash boot_a boot-zx500.img`
- Windows(_b): `uuu FB: flash boot_b boot-zx500.img`

### リージョン変更の方法
UAE、SEA、HK、SKおよびオセアニア市場向けに、ZX500のボリューム上限を開放できます。A100は未確認で、おそらく効果はないです。

1. 上記すべての作業を行う(ブートローダーのアンロック、avbの無効化、カーネルのフラッシュ)
2. KernelSUアプリを[こちら](https://github.com/tiann/KernelSU/releases/download/v0.6.7/KernelSU_v0.6.7_11210-release.apk)からダウンロード・インストールする
3. KernelSUアプリを開き、Shell(com.android.shell)のスーパーユーザーを有効にする、
4. 開発者オプションでADBデバッグを有効化する
5. USBでWalkmanをPCに接続する。`adb shell`を実行してShellに入る
6. `su`を実行してroot権限を取得する
7. `nvpflag shp 0x00000006 0x00000000`を実行し、次に `nvpflag sid 0x00000000`を実行してdestination codeを E に切り替える
8. USB接続を外し、Walkmanを再起動する。


## アップデートファイルの解凍

まず、デバイスのkey stringが必要です。adbを有効にして、`adb shell cat /vendor/usr/data/icx_nvp.cfg`を実行します。キー文字列はNAS sectionにあります。  
次に`java -version`を実行し、パスに java バージョン`>1.8`があることを確認します。ファームウェア復号化ツールは[ここ](https://github.com/notcbw/2019_android_walkman/releases/download/v0/nwwmdecrypt.jar)からダウンロードしてください。PowerShellなどのターミナルアプリで`java -jar nwwmdecrypt.jar -i <input file> -o <output file> -k <key string>`を実行して、復号化ツールを実行します。

復号後、zipファイルを解凍します。[payload_dumper](https://github.com/vm03/payload_dumper)を使用して、解凍したzipファイル内のpayload.binファイルを解凍します。

### 解凍済みファームウェアファイル

- NW-ZX500シリーズ v4.06 国際版ROM: [https://drive.google.com/file/d/1TUFwOOrex2miPd41UAhe8ioKbxIv4M0R/view?usp=sharing](https://drive.google.com/file/d/1TUFwOOrex2miPd41UAhe8ioKbxIv4M0R/view?usp=sharing)
- NW-ZX500シリーズ v4.04 中国版ROM (GAppsが無いので、少しだけバッテリー持ちが良いです): [https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing](https://drive.google.com/file/d/1z8CucsLx0LJ-0HU50QxVYnx8VHVroP7U/view?usp=sharing)
- NW-A100シリーズ v4.06 国際版ROM: [https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing](https://drive.google.com/file/d/1hiNf9VFeh0osPwbGtI2NeH9T5AHZtUJK/view?usp=sharing)

## 調査結果
【訳者追記】[こちらの記事](https://note.com/forsaken_love02/n/nbfe0c8f87f3c)に一通りの事はまとめました。  
【For English Users】Please read [this article](https://note.com/forsaken_love02/n/nbfe0c8f87f3c). If you can't read japanese, use translate app.  

### Fastbootに入るボタンコンボ
電源オン時にVol-とFFを押し続けます。

### アップデート用ファームウェアファイルについて  
ファームウェアアップデートファイルの最初の128バイトには、ファイルのmagicとSHA-228ダイジェストを含まれています。  
最初のバイトはmagic"NWWM "で、次の56バイトはASCII 16進数で格納されたSHA-224ダイジェストです。残りは不明です。  

暗号化されたデータは、標準的なAndroid OTAアップデートのzipファイルです。変換スキームはAES/CBC/PKCS5Paddingです。  

暗号化キーはプレーンテキストで`/vendor/usr/data/icx_nvp.cfg`に48文字長のASCIIテキストとして格納されています。  
最初の 32 バイトがAESキーで、次の 16 バイトが初期化ベクターです。NW-A100シリーズとNW-ZX500シリーズではキーが異なります。  

### NVPについて
- コンフィギュレーション、フラグ、キーなどはすべてraw列としてnvpに格納されています。
- `vendor/bin`にある `nvp`、`nvpflag`、`nvpinfo`、`nvpnode`、`nvpstr` および `nvptest` は、nvp の値を操作するために使用されるデバッグツールであると考えられます。
- `nvp` はバイナリのパーティションを16進形式で表示するために使用されます。
- `nvpflag` はdestinationなどのフラグを表示したり書き込んだりするのに使用されます。
- `nvpstr` はnvpの他の文字列変数を制御します。
- その他の変数の使用目的は不明です。
