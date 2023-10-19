package com.notcbw.walkmanusbaudio;

import androidx.appcompat.app.AppCompatActivity;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.provider.DocumentsContract;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import com.notcbw.walkmanusbaudio.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'walkmanusbaudio' library on application startup.
    static {
        System.loadLibrary("walkmanusbaudio");
    }

    private ActivityMainBinding binding;
    private boolean mUsbServiceBound = false;
    private WmUsbAudioService mService;

    private boolean mListening = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Example of a call to a native method
        TextView tv = binding.sampleText;
        tv.setText(stringFromJNI());

        Button testServiceButton = findViewById(R.id.test_service_button);
        testServiceButton.setText("Start Listening");
        testServiceButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mListening) {
                    if (mUsbServiceBound)
                        mService.endTestUeventCapturing();
                    testServiceButton.setText("Start Listening");
                    mListening = false;
                } else {
                    if (!mUsbServiceBound) {
                        Intent intent = new Intent(MainActivity.this, WmUsbAudioService.class);
                        WmUsbAudioService.bind(intent, mConnection);
                    }
                    mService.startTestUeventCapturing();
                    testServiceButton.setText("End Listening");
                    mListening = true;
                }
            }
        });
    }

    @Override
    protected void onStart() {
        super.onStart();
        Intent intent = new Intent(this, WmUsbAudioService.class);
        WmUsbAudioService.bind(intent, mConnection);
    }

    @Override
    protected void onStop() {
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

    private ServiceConnection mConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            WmUsbAudioService.WmUsbAudioBinder binder = (WmUsbAudioService.WmUsbAudioBinder) service;
            mService = binder.getService();
            mUsbServiceBound = true;
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            mUsbServiceBound = false;
        }
    };

    /**
     * A native method that is implemented by the 'walkmanusbaudio' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();

}