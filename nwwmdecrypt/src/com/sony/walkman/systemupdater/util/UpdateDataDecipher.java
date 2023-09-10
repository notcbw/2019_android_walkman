package com.sony.walkman.systemupdater.util;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.security.MessageDigest;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

public final class UpdateDataDecipher {
    private String mHash = null;
    private File mOut;
    private File mSrc;
    private String keyString;

    public UpdateDataDecipher(File source, File output, String keyString) {
        this.mSrc = null;
        this.mOut = null;
        this.mSrc = source;
        this.mOut = output;
        this.keyString = keyString;
    }

    public void decipher() throws DecihperErrorException {
        int ret = 0;
        int outputBytes;
        UpdateDataDecipher updateDataDecipher = this;
        boolean isLast = false;
        byte[] data = new byte[128];
        try { // load first 128 bytes to buffer
            FileInputStream fis = new FileInputStream(updateDataDecipher.mSrc); // load first 128 bytes to buffer
            int ret2 = fis.read(data, 0, 128);
            if (ret2 != 128) {
                fis.close();
                throw new IOException("File read error : ret " + ret2);
            }
            fis.close();
            UpdateMetaStore metaSt = new UpdateMetaStore(data, keyString);
            if (!metaSt.isMagicCorrect()) { // check if magic is correct ("NWWM")
                throw new DecihperErrorException("Magic error. can not update.");
            }
            try {
                SecretKeySpec keySpec = new SecretKeySpec(metaSt.getKey(), "AES");
                IvParameterSpec ivSpec = new IvParameterSpec(metaSt.getIv()); // get keys from izmprop
                Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
                cipher.init(2, keySpec, ivSpec);
                updateDataDecipher.mOut.delete();
                updateDataDecipher.mOut.createNewFile();
                ByteBuffer inBuf = ByteBuffer.allocate(160000);
                FileChannel inFc = new FileInputStream(updateDataDecipher.mSrc).getChannel();
                inFc.position(128L);
                FileChannel outFc = new FileOutputStream(updateDataDecipher.mOut).getChannel();
                MessageDigest sha228 = MessageDigest.getInstance("SHA-224");
                byte[] hash = null;
                while (true) {
                    inBuf.clear();
                    while (true) {
                        try {
                            ret = inFc.read(inBuf);
                        } catch (Exception e) {
                            e = e;
                        }
                        if (ret == -1) {
                            try {
                                //Log.v("WMU_dec", "Last block!!");
                                isLast = true;
                                break;
                            } catch (Exception e) {
                                //e = e2;
                                isLast = true;
                                //Log.e("WMU_dec", "Read error " + e);
                                updateDataDecipher = this;
                            }
                        } else if (ret == 0) {
                            break;
                        } else {
                            updateDataDecipher = this;
                        }
                    }
                    int outSz = cipher.getOutputSize(inBuf.position());
                    ByteBuffer outBuf = ByteBuffer.allocate(cipher.getOutputSize(outSz));
                    inBuf.flip();
                    if (isLast) {
                        try {
                            outputBytes = cipher.doFinal(inBuf, outBuf);
                            outBuf.flip();
                            sha228.update(outBuf);
                            byte[] hash2 = sha228.digest();
                            hash = hash2;
                        } catch (Exception e3) {
                            throw new DecihperErrorException("Process Cipher error: " + e3);
                        }
                    } else {
                        outputBytes = cipher.update(inBuf, outBuf);
                        outBuf.flip();
                        sha228.update(outBuf);
                    }
                    outBuf.flip();
                    int outDone = outFc.write(outBuf);
                    if (outDone != outputBytes) {
                        break;
                    } else if (!isLast) {
                        updateDataDecipher = this;
                    } else {
                        try {
                            inFc.close();
                            outFc.close();
                            //Log.v("WMU_dec", "New : " + HexDump.toHexString(hash));
                            //Log.v("WMU_dec", "Old : " + HexDump.toHexString(metaSt.getSum()));
                            if (!metaSt.isSha228DigestSame(hash)) {
                                updateDataDecipher.mOut.delete();
                                throw new DecihperErrorException("Checksum does not match!");
                            } else {
                                Boolean.valueOf(updateDataDecipher.mSrc.delete());
                                return;
                            }
                        } catch (Exception e4) {
                            throw new DecihperErrorException("File Close Error: " + e4);
                        }
                    }
                }
            } catch (Exception e5) {
                //Log.e("WMU_dec", "de-Cipher init op error : " + e5);
                throw new DecihperErrorException("Decipher init error: " + e5);
            }
        } catch (Exception e6) {
            throw new DecihperErrorException("Header process error:" + e6);
        }
    }

    /* loaded from: classes.dex */
    public static class DecihperErrorException extends Exception {
        DecihperErrorException(String msg) {
            super(msg);
        }
    }

    /* loaded from: classes.dex */
    public final class UpdateMetaStore {
        private final byte[] HEADER_SC;
        private byte[] mIv;
        private byte[] mKey;
        private byte[] mSc;
        private byte[] mSum;
        private byte[] mSumRaw;
        private final byte[] tmpIv;
        private final byte[] tmpKey256;

        private UpdateMetaStore(byte[] headerByte, String keyString) {
            this.tmpKey256 = "test_for_aes256bit_decode_perfom".getBytes();
            this.tmpIv = "test_iv_stringzz".getBytes();
            this.HEADER_SC = new byte[]{78, 87, 87, 77};
            this.mSc = new byte[4];
            this.mKey = new byte[32];
            this.mIv = new byte[16];
            this.mSumRaw = new byte[56];
            this.mSum = new byte[28];
            UpdateProperties prop = new UpdateProperties(keyString);
            this.mKey = prop.getKeyBytes();
            this.mIv = prop.getIvBytes();
            System.arraycopy(headerByte, 0, this.mSc, 0, 4);
            System.arraycopy(headerByte, 4, this.mSumRaw, 0, 56);
            String tmpStr = new String(this.mSumRaw);
            this.mSum = HexDump.hexStringToByteArray(tmpStr);
            //Log.v("WMU_dec", "sum:" + HexDump.toHexString(this.mSum));
        }

        /* JADX INFO: Access modifiers changed from: private */
        public byte[] getKey() {
            return this.mKey;
        }

        /* JADX INFO: Access modifiers changed from: private */
        public byte[] getIv() {
            return this.mIv;
        }

        /* JADX INFO: Access modifiers changed from: private */
        public byte[] getSum() {
            return this.mSum;
        }

        /* JADX INFO: Access modifiers changed from: private */
        public boolean isMagicCorrect() {
            return Arrays.equals(this.mSc, this.HEADER_SC);
        }

        /* JADX INFO: Access modifiers changed from: private */
        public boolean isSha228DigestSame(byte[] digest) {
            return Arrays.equals(digest, this.mSum);
        }
    }
}
