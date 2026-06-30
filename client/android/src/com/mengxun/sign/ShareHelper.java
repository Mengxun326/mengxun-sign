package com.mengxun.sign;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.util.Log;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpCookie;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.security.MessageDigest;
import java.util.List;

public class ShareHelper extends Service {
    private static final String TAG = "ShareHelper";
    private static final String CHANNEL_ID = "share_sign";
    private static final int NOTIFY_ID = 2001;
    private static final String SERVER = "https://xxt.meng-xun.top";
    private static final String CX_TOKEN = "4faa8662c59590c6f43ae9fe5b002b42";
    private static final String CX_DES_KEY = "Z(AfY@XS";
    private static String sToken;
    private Handler mHandler;
    private Runnable mPollRunnable;

    public static void start(Context ctx, String token) {
        sToken = token;  // Just update token — service already running
        Intent intent = new Intent(ctx, ShareHelper.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            ctx.startForegroundService(intent);
        } else {
            ctx.startService(intent);
        }
    }

    public static void stop(Context ctx) {
        sToken = null;
        ctx.stopService(new Intent(ctx, ShareHelper.class));
    }

    @Override public void onCreate() {
        super.onCreate();
        Log.i(TAG, "onCreate");
        mHandler = new Handler();
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i(TAG, "onStartCommand");
        createChannel();
        PendingIntent pi = PendingIntent.getActivity(this, 0,
            new Intent(this, org.qtproject.qt.android.bindings.QtActivity.class),
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        startForeground(NOTIFY_ID,
            new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("梦寻签到").setContentText("正在等待帮签请求...")
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentIntent(pi).setOngoing(true).build());
        startPolling();
        return START_STICKY;
    }

    @Override public IBinder onBind(Intent i) { return null; }

    @Override public void onDestroy() {
        Log.i(TAG, "onDestroy");
        stopPolling();
        stopForeground(true);
        super.onDestroy();
    }

    private void startPolling() {
        mPollRunnable = new Runnable() { public void run() {
            new Thread(() -> poll()).start();
            mHandler.postDelayed(this, 1000);
        }};
        mHandler.post(mPollRunnable);
    }

    private void stopPolling() {
        if (mPollRunnable != null) mHandler.removeCallbacks(mPollRunnable);
    }

    private void poll() {
        if (sToken == null || sToken.isEmpty()) return;
        try {
            URL url = new URL(SERVER + "/api/share/pending/" + sToken);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(5000); conn.setReadTimeout(5000);
            BufferedReader br = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            StringBuilder sb = new StringBuilder(); String line;
            while ((line = br.readLine()) != null) sb.append(line);
            br.close(); conn.disconnect();
            String resp = sb.toString();
            if (resp.contains("\"phone\"") && resp.length() > 10) {
                Log.i(TAG, "Pending request found, processing...");
                try {
                    org.json.JSONArray arr = new org.json.JSONArray(resp);
                    if (arr.length() > 0) {
                        String reqId = arr.getJSONObject(0).getString("id");
                        signAndReport(sToken, reqId);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Parse pending err: " + e.getMessage());
                }
            }
        } catch (Exception e) { Log.e(TAG, "Poll err: " + e.getMessage()); }
    }

    private void signAndReport(String token, String reqId) {
        try {
            URL url = new URL(SERVER + "/api/share/sign-params/" + token);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(5000); conn.setReadTimeout(5000);
            BufferedReader br = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            StringBuilder sb = new StringBuilder(); String line;
            while ((line = br.readLine()) != null) sb.append(line);
            br.close(); conn.disconnect();
            String body = sb.toString();

            org.json.JSONObject params = new org.json.JSONObject(body);
            String activeId = params.optString("active_id", "");
            String uid = params.optString("uid", "");
            String fid = params.optString("fid", "0");
            String name = params.optString("name", "");
            String phone = params.optString("phone", "");
            String password = params.optString("password", "");
            String enc = params.optString("enc", "");
            String lat = params.optString("lat", "");
            String lon = params.optString("lon", "");
            String cookiesJson = params.optString("cookies_json", "");
            Log.i(TAG, "Params: a=" + activeId + " phone=" + phone + " enc=" + enc);

            if (activeId.isEmpty()) { reportResult(reqId, false, "no params"); return; }

            // Try fresh login first (stored cookies expire in ~30s)
            String cookieHeader = "";
            if (!phone.isEmpty() && !password.isEmpty()) {
                cookieHeader = loginToChaoxing(phone, password);
                Log.i(TAG, "Fresh login: " + (cookieHeader.isEmpty() ? "FAILED" : "OK " + cookieHeader.length() + " chars"));
            }
            if (cookieHeader.isEmpty()) {
                cookieHeader = buildCookieHeader(cookiesJson);
                Log.i(TAG, "Fallback stored cookies: " + (cookieHeader.isEmpty() ? "NONE" : cookieHeader.length() + " chars"));
            }
            if (cookieHeader.isEmpty()) { reportResult(reqId, false, "login failed"); return; }

            String cxUrl = "https://mobilelearn.chaoxing.com/pptSign/stuSignajax"
                + "?activeId=" + activeId + "&uid=" + uid + "&fid=" + fid
                + "&appType=15&ifTiJiao=1"
                + "&name=" + URLEncoder.encode(name, "UTF-8")
                + "&address=" + URLEncoder.encode("中国", "UTF-8")
                + "&latitude=" + (lat.isEmpty() ? "-1" : lat)
                + "&longitude=" + (lon.isEmpty() ? "-1" : lon)
                + "&clientip=";
            if (!enc.isEmpty())
                cxUrl += "&enc=" + URLEncoder.encode(enc, "UTF-8");
            URL cxU = new URL(cxUrl);
            HttpURLConnection cxReq = (HttpURLConnection) cxU.openConnection();
            cxReq.setRequestProperty("User-Agent", "Mozilla/5.0");
            cxReq.setRequestProperty("Accept", "application/json, text/plain, */*");
            cxReq.setRequestProperty("X-Requested-With", "XMLHttpRequest");
            cxReq.setRequestProperty("Cookie", cookieHeader);
            cxReq.setConnectTimeout(10000); cxReq.setReadTimeout(10000);
            int cxCode = cxReq.getResponseCode();
            BufferedReader cxBr = new BufferedReader(new InputStreamReader(cxReq.getInputStream()));
            StringBuilder cxSb = new StringBuilder();
            while ((line = cxBr.readLine()) != null) cxSb.append(line);
            cxBr.close(); cxReq.disconnect();
            String result = cxSb.toString();
            Log.i(TAG, "CX sign body(" + result.length() + "): " + result.substring(0, Math.min(300, result.length())));
            boolean isLoginPage = result.contains("用户登录") || result.contains("请先登录");
            boolean ok = !isLoginPage && result.length() < 500
                && (result.contains("成功") || result.contains("success"));
            Log.i(TAG, "Sign result: " + (ok ? "OK" : "FAIL"));
            reportResult(reqId, ok, result);
        } catch (Exception e) {
            Log.e(TAG, "Sign err: " + e.getClass().getName() + " " + e.getMessage());
            try { reportResult(reqId, false, e.getClass().getName() + ":" + e.getMessage()); } catch (Exception e2) {}
        }
    }

    private String loginToChaoxing(String phone, String password) {
        try {
            java.net.CookieManager cm = new java.net.CookieManager();
            java.net.CookieHandler.setDefault(cm);
            String ts = String.valueOf(System.currentTimeMillis());

            // Proper inf_enc = MD5("token=TOKEN&_time=TS&DESKey=DES_KEY")
            String plain = "token=" + CX_TOKEN + "&_time=" + ts + "&DESKey=" + CX_DES_KEY;
            MessageDigest md = MessageDigest.getInstance("MD5");
            byte[] hash = md.digest(plain.getBytes("UTF-8"));
            StringBuilder hex = new StringBuilder();
            for (byte b : hash) hex.append(String.format("%02x", b));
            String infEnc = hex.toString();

            // Step 1: POST login
            String loginUrl = "https://passport2-api.chaoxing.com/v11/loginregister"
                + "?token=" + CX_TOKEN + "&_time=" + ts + "&inf_enc=" + infEnc;
            HttpURLConnection conn = (HttpURLConnection) new URL(loginUrl).openConnection();
            conn.setRequestProperty("User-Agent", "Mozilla/5.0");
            conn.setDoOutput(true);
            String postData = "uname=" + URLEncoder.encode(phone, "UTF-8")
                + "&code=" + URLEncoder.encode(password, "UTF-8")
                + "&loginType=1&roleSelect=true";
            conn.getOutputStream().write(postData.getBytes());
            int code = conn.getResponseCode();
            BufferedReader br = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            StringBuilder resp = new StringBuilder(); String line;
            while ((line = br.readLine()) != null) resp.append(line);
            br.close(); conn.disconnect();
            Log.i(TAG, "CX login: " + code + " body: " + resp.substring(0, Math.min(80, resp.length())));
            if (code != 200 || resp.toString().contains("false")) return "";

            // Step 2: Get cookies from all Chaoxing subdomains (matching C++ syncCookiesFromJar)
            String[] cookieUrls = {
                "https://passport2.chaoxing.com/api/cookie",
                "https://i.chaoxing.com/base",
                "https://mooc1-2.chaoxing.com/visit/courses",
                "https://mobilelearn.chaoxing.com/pptSign/stuSignajax",
                "https://chaoxing.com",
            };
            for (String u : cookieUrls) {
                try {
                    HttpURLConnection c = (HttpURLConnection) new URL(u).openConnection();
                    c.setRequestProperty("User-Agent", "Mozilla/5.0");
                    c.setConnectTimeout(3000); c.setReadTimeout(3000);
                    c.getResponseCode();
                    BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream()));
                    while ((line = r.readLine()) != null); // consume body → triggers cookie storage
                    r.close(); c.disconnect();
                } catch (Exception e) { /* ignore — domain might not be needed */ }
            }

            // Collect all cookies
            List<HttpCookie> cookies = cm.getCookieStore().getCookies();
            StringBuilder sb = new StringBuilder();
            for (HttpCookie c : cookies) {
                if (sb.length() > 0) sb.append("; ");
                sb.append(c.getName()).append("=").append(c.getValue());
            }
            Log.i(TAG, "Fresh login OK: " + cookies.size() + " cookies, " + sb.length() + " chars");
            return sb.toString();
        } catch (Exception e) {
            Log.e(TAG, "CX login err: " + e.getMessage());
            return "";
        }
    }

    private String buildCookieHeader(String cookiesJson) {
        if (cookiesJson == null || cookiesJson.isEmpty() || cookiesJson.equals("{}"))
            return "";
        try {
            org.json.JSONObject co = new org.json.JSONObject(cookiesJson);
            StringBuilder sb = new StringBuilder();
            java.util.Iterator<String> keys = co.keys();
            while (keys.hasNext()) {
                String k = keys.next();
                if (sb.length() > 0) sb.append("; ");
                sb.append(k).append("=").append(co.getString(k));
            }
            return sb.toString();
        } catch (Exception e) { return ""; }
    }

    private void reportResult(String reqId, boolean success, String msg) {
        try {
            String data = "req_id=" + reqId + "&status=" + (success ? "success" : "failed") + "&result_msg=" + URLEncoder.encode(msg.length() > 100 ? msg.substring(0, 100) : msg, "UTF-8");
            URL url = new URL(SERVER + "/api/share/result");
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setDoOutput(true);
            conn.getOutputStream().write(data.getBytes());
            conn.getResponseCode();
            conn.disconnect();
        } catch (Exception e) { Log.e(TAG, "Report err: " + e.getMessage()); }
    }

    private void createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel ch = new NotificationChannel(CHANNEL_ID, "分享帮签", NotificationManager.IMPORTANCE_LOW);
            ch.setShowBadge(false);
            ((NotificationManager)getSystemService(Context.NOTIFICATION_SERVICE)).createNotificationChannel(ch);
        }
    }
}
