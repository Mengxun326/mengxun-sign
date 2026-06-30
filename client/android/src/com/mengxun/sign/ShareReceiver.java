package com.mengxun.sign;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Receives SHARE_PENDING broadcast from ShareHelper and notifies the Qt app
 * to process the pending sign-in using its live Chaoxing session cookies.
 */
public class ShareReceiver extends BroadcastReceiver {
    private static final String TAG = "ShareReceiver";

    // Called from Qt C++ via JNI to register the callback
    public static native void onSharePending();

    @Override
    public void onReceive(Context context, Intent intent) {
        Log.i(TAG, "Broadcast received, waking Qt app for share sign-in");
        try {
            onSharePending();
        } catch (Exception e) {
            Log.e(TAG, "onSharePending err: " + e.getMessage());
        }
    }
}
