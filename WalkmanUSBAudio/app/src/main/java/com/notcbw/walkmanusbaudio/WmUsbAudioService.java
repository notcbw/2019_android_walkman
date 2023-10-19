package com.notcbw.walkmanusbaudio;

import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.util.Log;

import androidx.annotation.NonNull;

import com.topjohnwu.superuser.ipc.RootService;

public class WmUsbAudioService extends RootService {
    private static final String LOG_TAG = "WmUsbAudioService";
    private IBinder mBinder = new WmUsbAudioBinder();

    @Override
    public void onCreate() {
        super.onCreate();
        Log.v(LOG_TAG, "Service created");
    }

    @Override
    public IBinder onBind(@NonNull Intent intent) {
        Log.v(LOG_TAG, "Service binded");
        return mBinder;
    }

    @Override
    public void onDestroy() {
        Log.v(LOG_TAG, "Service destroryed");
    }

    public class WmUsbAudioBinder extends Binder {
        WmUsbAudioService getService() {
            return WmUsbAudioService.this;
        }
    }

    public native void startTestUeventCapturing();
    public native void endTestUeventCapturing();
}
