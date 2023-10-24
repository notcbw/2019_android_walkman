// IWmUsbAudioService.aidl
package com.notcbw.walkmanusbaudio;

import com.notcbw.walkmanusbaudio.IWmUsbAudioServiceCallback;
// Declare any non-default types here with import statements

interface IWmUsbAudioService {
    void startTestUeventCapturing();
    void endTestUeventCapturing();
    void startUsbAudioPipeline(int interfaceType);
    void endUsbAudioPipeline();
    boolean pipelineIsStarted();
    int getSupportedInterfaces();
    void registerCallback(IWmUsbAudioServiceCallback cb);
}