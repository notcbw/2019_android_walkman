package com.notcbw.walkmanusbaudio;

import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.Process;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.util.Log;

import androidx.annotation.NonNull;

import com.topjohnwu.superuser.Shell;
import com.topjohnwu.superuser.ipc.RootService;

public class WmUsbAudioService extends RootService {
    private static final String LOG_TAG = "WmUsbAudioService";

    static {
        if (Process.myUid() == 0)
            System.loadLibrary("walkmanusbaudio");
    }

    final RemoteCallbackList<IWmUsbAudioServiceCallback> mCallbacks =
            new RemoteCallbackList<IWmUsbAudioServiceCallback>();
    private PowerManager pm;
    private PowerManager.WakeLock lock;

    @Override
    public void onCreate() {
        Log.d(LOG_TAG, "WmUsbAudioService onCreate");
        super.onCreate();
        pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        lock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "WmUsbAudioService::Wakelock");
    }

    @Override
    public IBinder onBind(@NonNull Intent intent) {
        Log.d(LOG_TAG, "WmUsbAudioService onBind");
        return new WmUsbAudioIPC();
    }

    @Override
    public void onDestroy() {
        Log.d(LOG_TAG, "WmUsbAudioService onDestroy");
        super.onDestroy();
    }

    class WmUsbAudioIPC extends IWmUsbAudioService.Stub {

        @Override
        public void startTestUeventCapturing() throws RemoteException {
            return;
        }

        @Override
        public void endTestUeventCapturing() throws RemoteException {
            return;
        }

        @Override
        public void startUsbAudioPipeline(int interfaceType) throws RemoteException {
            int rtn = nativeStartUsbAudioPipeline(interfaceType);
            final String errs = "Failed to start pipeline: ";
            switch (rtn) {
                case 0:
                    pipelineRunning = true;
                    lock.acquire();
                    return;
                case -1:
                    throw new RemoteException(errs + "Failed to get field ID");
                case 1:
                    throw new RemoteException(String.format(errs + "Interface type %d is not valid!", interfaceType));
                case 2:
                    throw new RemoteException(errs + "Failed to instantiate Playback Interface!");
                case 3:
                    throw new RemoteException(errs + "Failed to instantiate UacStatusUIUpdater");
                case 4:
                    throw new RemoteException(errs + "Failed to instantiate UacSink");
                case 5:
                    throw new RemoteException(errs + "Failed to instantiate UacEventListener");
                default:
                    throw new RemoteException(errs + "What?");
            }
        }

        @Override
        public void endUsbAudioPipeline() throws RemoteException {
            nativeEndUsbAudioPipeline();
            pipelineRunning = false;
            lock.release();
        }

        @Override
        public boolean pipelineIsStarted() throws RemoteException {
            return pipelineRunning;
        }

        @Override
        public int getSupportedInterfaces() throws RemoteException {
            return nativeGetSupportedInterfaces();
        }

        @Override
        public void registerCallback(IWmUsbAudioServiceCallback cb) throws RemoteException {
            if (cb != null) {
                mCallbacks.register(cb);
            }
        }
    }

    void callbackUIUpdate(int state, int format, int freq, int bitwidth) {
        try {
            int n = mCallbacks.beginBroadcast();
            Log.d(LOG_TAG, "mCallBacks N value = " + n);
            for (int i = 0; i < n; i++)
                mCallbacks.getBroadcastItem(i).cbSetUacStatus(state, format, freq, bitwidth);
            mCallbacks.finishBroadcast();
        } catch (RemoteException e) {
            throw new RuntimeException(e);
        }
    }

    private long mUeventListenerPtr = 0;
    private long mUacSinkPtr = 0;
    private long mInterfacePtr = 0;
    private long mUIUpdaterPtr = 0;
    private int mCurIfType = 0;
    private boolean pipelineRunning = false;

    private native int nativeStartUsbAudioPipeline(int ifType);
    private native void nativeEndUsbAudioPipeline();
    private native int nativeGetSupportedInterfaces();
}
