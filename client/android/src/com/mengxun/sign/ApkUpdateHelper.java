package com.mengxun.sign;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import androidx.core.content.FileProvider;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import org.json.JSONObject;

public class ApkUpdateHelper {
    private static final String CHECK_URL = "https://xxt.meng-xun.top/api/update/check";
    private static final String DOWNLOAD_URL = "https://xxt.meng-xun.top/api/update/download";

    public static void doUpdate(Activity activity) {
        new Thread(() -> {
            try {
                // 1. Check server version
                HttpURLConnection conn = (HttpURLConnection) new URL(CHECK_URL).openConnection();
                conn.setConnectTimeout(5000); conn.setReadTimeout(5000);
                InputStream is = conn.getInputStream();
                StringBuilder sb = new StringBuilder(); byte[] buf = new byte[4096]; int n;
                while ((n = is.read(buf)) > 0) sb.append(new String(buf, 0, n));
                is.close(); conn.disconnect();
                JSONObject json = new JSONObject(sb.toString());
                int serverCode = json.getInt("versionCode");
                String ver = json.getString("version");
                if (serverCode <= activity.getPackageManager()
                        .getPackageInfo(activity.getPackageName(), 0).getLongVersionCode()) return;

                // 2. Show dialog on UI thread
                Handler mainHandler = new Handler(Looper.getMainLooper());
                final boolean[] doUpdate = {false};
                final Object lock = new Object();
                mainHandler.post(() -> {
                    new AlertDialog.Builder(activity)
                        .setTitle("发现新版本")
                        .setMessage("版本 " + ver + " 可用，是否更新？")
                        .setPositiveButton("更新", (d, w) -> { doUpdate[0] = true; synchronized(lock) { lock.notify(); } })
                        .setNegativeButton("取消", (d, w) -> { synchronized(lock) { lock.notify(); } })
                        .show();
                });
                synchronized(lock) { lock.wait(); }
                if (!doUpdate[0]) return;

                // 3. Download
                conn = (HttpURLConnection) new URL(DOWNLOAD_URL).openConnection();
                conn.setConnectTimeout(15000); conn.setReadTimeout(60000);
                is = conn.getInputStream();
                File dir = activity.getExternalCacheDir();
                if (dir == null) dir = activity.getCacheDir();
                File apk = new File(dir, "update.apk");
                FileOutputStream fos = new FileOutputStream(apk);
                byte[] buf2 = new byte[8192]; int n2;
                while ((n2 = is.read(buf2)) > 0) fos.write(buf2, 0, n2);
                fos.close(); is.close(); conn.disconnect();

                // 4. Install
                mainHandler.post(() -> {
                    Uri uri = FileProvider.getUriForFile(activity,
                        activity.getPackageName() + ".fileprovider", apk);
                    Intent intent = new Intent(Intent.ACTION_VIEW);
                    intent.setDataAndType(uri, "application/vnd.android.package-archive");
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    activity.startActivity(intent);
                });
            } catch (Exception e) {
                // silent fail
            }
        }).start();
    }
}
