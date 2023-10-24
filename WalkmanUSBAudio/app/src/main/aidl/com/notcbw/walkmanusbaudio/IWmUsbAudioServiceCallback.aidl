// IWmUsbAudioServiceCallback.aidl
package com.notcbw.walkmanusbaudio;

// Declare any non-default types here with import statements

interface IWmUsbAudioServiceCallback {
    void cbSetUacStatus(int state, int format, int freq, int bitwidth);
}