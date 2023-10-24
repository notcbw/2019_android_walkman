package com.notcbw.walkmanusbaudio;

import androidx.appcompat.app.AppCompatActivity;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.provider.DocumentsContract;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.TextView;

import com.notcbw.walkmanusbaudio.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'walkmanusbaudio' library on application startup.
    static {
        System.loadLibrary("walkmanusbaudio");
    }

    private ServiceConnection mConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            Log.d(LOG_TAG, "WmUsbAudioService onServiceConnected");
            mService = IWmUsbAudioService.Stub.asInterface(service);
            mUsbServiceBound = true;
            try {
                mService.registerCallback(new WmUsbAudioCallback());

                // disable unsupported interface type buttons
                int serv = mService.getSupportedInterfaces();
                if ((serv & 0b001) > 0)
                    ifRadioGroup.getChildAt(0).setEnabled(true);
                else
                    ifRadioGroup.getChildAt(0).setEnabled(false);

                if ((serv & 0b010) > 0)
                    ifRadioGroup.getChildAt(1).setEnabled(true);
                else
                    ifRadioGroup.getChildAt(1).setEnabled(false);

                if ((serv & 0b100) > 0)
                    ifRadioGroup.getChildAt(2).setEnabled(true);
                else
                    ifRadioGroup.getChildAt(2).setEnabled(false);
            } catch (RemoteException e) {
                throw new RuntimeException(e);
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.d(LOG_TAG, "WmUsbAudioService onServiceDisconnected");
            mUsbServiceBound = false;
        }
    };

    class WmUsbAudioCallback extends IWmUsbAudioServiceCallback.Stub {
        @Override
        public void cbSetUacStatus(int state, int format, int freq, int bitwidth) throws RemoteException {
            String status = "";
            String audioFormat = "";

            switch (state) {
                case 1:
                    status = "Playing: ";
                    break;
                case 2:
                    status = "Stopped: ";
                    break;
                default:
                    status = "Idle.";
            }

            if (state == 0) {
                switch (format) {
                    case 1:
                        audioFormat = String.format("PCM %dHz/%d-bit", freq, bitwidth);
                        break;
                    case 2:
                        float dsdFreq = (float)freq / 100000.0f;
                        audioFormat = String.format("DSD %fMHz", dsdFreq);
                        break;
                    default:
                        audioFormat = "";
                }
            }

            statusText.setText(status + audioFormat);
        }
    }

    private static final String LOG_TAG = "MainActivity";
    private ActivityMainBinding binding;
    private TextView statusText;
    private RadioGroup ifRadioGroup;
    private int ifType;
    private boolean mUsbServiceBound = false;
    private IWmUsbAudioService mService;
    private Intent mServiceIntent;
    private boolean mListening = false;
    private UsbGadgetController mUsbCtrl;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        mUsbCtrl = new UsbGadgetController();
        //mUsbCtrl.enableUAC2();
        //mUsbCtrl.disableMassStorage();

//        Button testServiceButton = findViewById(R.id.test_service_button);
//        testServiceButton.setText("Start Listening");
//        testServiceButton.setOnClickListener(v -> {
//            if (mListening) {
//                if (mUsbServiceBound) {
//                    try {
//                        mService.endTestUeventCapturing();
//                    } catch (RemoteException e) {
//                        throw new RuntimeException(e);
//                    }
//                }
//                testServiceButton.setText("Start Listening");
//                mListening = false;
//            } else {
//                if (!mUsbServiceBound) {
//                    Log.d(LOG_TAG, "service is dead, trying to restart");
//                    Intent intent = new Intent(this, WmUsbAudioService.class);
//                    WmUsbAudioService.bind(intent, mConnection);
//                }
//                try {
//                    mService.startTestUeventCapturing();
//                } catch (RemoteException e) {
//                    throw new RuntimeException(e);
//                }
//                testServiceButton.setText("End Listening");
//                mListening = true;
//            }
//        });

        statusText = findViewById(R.id.status_text);
        statusText.setText("Idle.");

        ifRadioGroup = findViewById(R.id.if_radio);
        ifRadioGroup.check(R.id.rb_null);
        ifType = 0;
        ifRadioGroup.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(RadioGroup radioGroup, int i) {
                if (i == R.id.rb_null) {
                    ifType = 0;
                } else if (i == R.id.rb_aaudio) {
                    ifType = 1;
                } else if (i == R.id.rb_spaudio) {
                    ifType = 2;
                } else {
                    throw new RuntimeException("Non-existent radio button????");
                }
            }
        });

        Button uacButton = findViewById(R.id.uac_button);
        uacButton.setText("Enable UAC2");
        uacButton.setOnClickListener(v -> {
            if (mUsbCtrl.uac2IsEnabled()) {
                mUsbCtrl.disableUAC2();
                mUsbCtrl.resetUsb();
                uacButton.setText("Enable UAC2");
            } else {
                mUsbCtrl.enableUAC2();
                mUsbCtrl.resetUsb();
                uacButton.setText("Disable UAC2");
            }
        });

        Button pipelineButton = findViewById(R.id.pipeline_button);
        pipelineButton.setText("Start Audio");
        pipelineButton.setOnClickListener(v -> {
            boolean started = false;
            try {
                started = mService.pipelineIsStarted();
            } catch (RemoteException e) {
                e.printStackTrace();
            }

            if (started) {
                try {
                    mService.endUsbAudioPipeline();
                } catch (RemoteException e) {
                    e.printStackTrace();
                } finally {
                    started = false;
                    pipelineButton.setText("Start Audio");
                }
            } else {
                try {
                    mService.startUsbAudioPipeline(ifType);
                } catch (RemoteException e) {
                    e.printStackTrace();
                } finally {
                    started = true;
                    pipelineButton.setText("End Audio");
                }
            }
        });

    }

    @Override
    protected void onStart() {
        Log.d(LOG_TAG, "MainActivity onStart");
        super.onStart();
        mServiceIntent = new Intent(this, WmUsbAudioService.class);
        WmUsbAudioService.bind(mServiceIntent, mConnection);
    }

    @Override
    protected void onStop() {
        Log.d(LOG_TAG, "MainActivity onStop");
        super.onStop();
        if (mUsbServiceBound) {
            unbindService(mConnection);
            mUsbServiceBound = false;
        }
    }

    @Override
    public void onBackPressed() {
        finish();
    }

    /**
     * A native method that is implemented by the 'walkmanusbaudio' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();

}