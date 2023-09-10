package com.sony.walkman.systemupdater.util;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class UpdateProperties {
    private byte[] mIvBytes;
    private byte[] mKeyBytes;

    public UpdateProperties(String keyString) {
        byte[] keyArr = keyString.getBytes(StandardCharsets.US_ASCII);
        mKeyBytes = Arrays.copyOfRange(keyArr, 0, 32);
        mIvBytes = Arrays.copyOfRange(keyArr, 32, 48);
    }

    public byte[] getKeyBytes() {
        return this.mKeyBytes;
    }

    public byte[] getIvBytes() {
        return this.mIvBytes;
    }
}
