package fr.boitiere85;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiNetworkSpecifier;
import android.os.Bundle;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * Application Boitier E85.
 *  - interface = assets/index.html (la meme que celle servie par le boitier)
 *  - connexion au Wi-Fi du boitier + liaison de l'appli a ce reseau
 *  - verification des nouvelles versions sur GitHub (par la 4G / le Wi-Fi internet)
 *  - installation de la mise a jour de l'appli
 */
public class MainActivity extends Activity {

    static final String REPO = "Alexh2807/boitier-e85";
    static final String ACTION_INSTALL = "fr.boitiere85.INSTALL_STATUS";

    private WebView web;
    private ConnectivityManager cm;
    private ConnectivityManager.NetworkCallback callback;
    private volatile String state = "Non connecté.";
    private volatile String latest = null, apkUrl = null, updError = null;
    private volatile boolean checked = false, downloading = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(Color.parseColor("#0d1117"));
        getWindow().setNavigationBarColor(Color.parseColor("#161b22"));
        cm = getSystemService(ConnectivityManager.class);

        web = new WebView(this);
        web.setBackgroundColor(Color.parseColor("#0d1117"));
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowFileAccessFromFileURLs(true);
        s.setAllowUniversalAccessFromFileURLs(true);   // file:// -> http://192.168.4.1
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        web.setWebViewClient(new WebViewClient());
        web.addJavascriptInterface(new Bridge(), "Android");
        web.loadUrl("file:///android_asset/index.html");
        setContentView(web);

        new Thread(this::checkUpdate).start();
    }

    // ------------------------------------------------------------------ Wi-Fi du boitier
    private void connectWifi(String ssid, String pass) {
        if (callback != null) {
            try { cm.unregisterNetworkCallback(callback); } catch (Exception ignored) { }
            callback = null;
        }
        cm.bindProcessToNetwork(null);
        state = "Recherche du Wi-Fi " + ssid + "…";

        WifiNetworkSpecifier.Builder b = new WifiNetworkSpecifier.Builder().setSsid(ssid);
        if (pass != null && pass.length() >= 8) b.setWpa2Passphrase(pass);
        NetworkRequest req = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .setNetworkSpecifier(b.build())
                .build();

        callback = new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network n) {
                cm.bindProcessToNetwork(n);
                state = "Connecté à " + ssid + ".";
            }
            @Override public void onLost(Network n) {
                cm.bindProcessToNetwork(null);
                state = "Connexion au boîtier perdue.";
            }
            @Override public void onUnavailable() {
                state = "Boîtier introuvable (" + ssid + "). Contact mis ? Mot de passe correct ?";
            }
        };
        try {
            cm.requestNetwork(req, callback, 30000);
        } catch (Exception e) {
            state = "Erreur Wi-Fi : " + e.getMessage();
        }
    }

    // ------------------------------------------------------------------ mises a jour (GitHub)
    /** Un reseau qui a vraiment internet (4G ou Wi-Fi maison), meme si l'appli est liee au boitier. */
    private Network internet() {
        for (Network n : cm.getAllNetworks()) {
            NetworkCapabilities c = cm.getNetworkCapabilities(n);
            if (c != null && c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    && c.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)) return n;
        }
        return null;
    }

    private HttpURLConnection open(String url) throws Exception {
        Network n = internet();
        if (n == null) throw new Exception("pas d'internet");
        HttpURLConnection c = (HttpURLConnection) n.openConnection(new URL(url));
        c.setConnectTimeout(8000);
        c.setReadTimeout(20000);
        c.setRequestProperty("User-Agent", "BoitierE85-app");
        c.setInstanceFollowRedirects(true);
        return c;
    }

    private String currentVersion() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (Exception e) { return "0"; }
    }

    static int cmpVersion(String a, String b) {
        String[] x = a.replaceAll("[^0-9.]", "").split("\\."), y = b.replaceAll("[^0-9.]", "").split("\\.");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            int p = i < x.length && !x[i].isEmpty() ? Integer.parseInt(x[i]) : 0;
            int q = i < y.length && !y[i].isEmpty() ? Integer.parseInt(y[i]) : 0;
            if (p != q) return Integer.compare(p, q);
        }
        return 0;
    }

    private void checkUpdate() {
        // quelques essais : au lancement, le reseau internet n'est pas toujours pret
        for (int k = 0; k < 6 && !checked; k++) {
            try {
                HttpURLConnection c = open("https://api.github.com/repos/" + REPO + "/releases/latest");
                c.setRequestProperty("Accept", "application/vnd.github+json");
                if (c.getResponseCode() != 200) throw new Exception("GitHub " + c.getResponseCode());
                JSONObject j = new JSONObject(readAll(c.getInputStream()));
                latest = j.getString("tag_name").replaceFirst("^v", "");
                JSONArray as = j.getJSONArray("assets");
                for (int i = 0; i < as.length(); i++) {
                    JSONObject a = as.getJSONObject(i);
                    if (a.getString("name").endsWith(".apk")) apkUrl = a.getString("browser_download_url");
                }
                checked = true; updError = null;
            } catch (Exception e) {
                updError = "vérification impossible : " + e.getMessage();
                try { Thread.sleep(10000); } catch (InterruptedException ignored) { return; }
            }
        }
    }

    private static String readAll(InputStream in) throws Exception {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192]; int r;
        while ((r = in.read(b)) > 0) o.write(b, 0, r);
        in.close();
        return o.toString("UTF-8");
    }

    private void downloadAndInstall() {
        if (downloading || apkUrl == null) return;
        downloading = true;
        new Thread(() -> {
            try {
                HttpURLConnection c = open(apkUrl);
                PackageInstaller pi = getPackageManager().getPackageInstaller();
                PackageInstaller.SessionParams p = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                int id = pi.createSession(p);
                try (PackageInstaller.Session s = pi.openSession(id)) {
                    try (InputStream in = c.getInputStream(); OutputStream out = s.openWrite("app.apk", 0, -1)) {
                        byte[] b = new byte[65536]; int r;
                        while ((r = in.read(b)) > 0) out.write(b, 0, r);
                        s.fsync(out);
                    }
                    Intent i = new Intent(this, MainActivity.class).setAction(ACTION_INSTALL);
                    PendingIntent pend = PendingIntent.getActivity(this, 0, i,
                            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
                    s.commit(pend.getIntentSender());
                }
            } catch (Exception e) {
                updError = "téléchargement impossible : " + e.getMessage();
                runOnUiThread(() -> Toast.makeText(this, updError, Toast.LENGTH_LONG).show());
            } finally {
                downloading = false;
            }
        }).start();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (!ACTION_INSTALL.equals(intent.getAction())) return;
        int st = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (st == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) startActivity(confirm);       // fenetre systeme "Installer ?"
        } else if (st != PackageInstaller.STATUS_SUCCESS) {
            String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            Toast.makeText(this, "Installation annulée" + (msg != null ? " : " + msg : ""), Toast.LENGTH_LONG).show();
        }
    }

    // ------------------------------------------------------------------ pont JavaScript
    private class Bridge {
        @JavascriptInterface public void connect(String ssid, String pass) { runOnUiThread(() -> connectWifi(ssid, pass)); }
        @JavascriptInterface public String netState() { return state; }
        @JavascriptInterface public void installUpdate() { downloadAndInstall(); }
        @JavascriptInterface public String updateInfo() {
            try {
                JSONObject o = new JSONObject();
                String cur = currentVersion();
                o.put("current", cur);
                o.put("checked", checked);
                if (latest != null) { o.put("latest", latest); o.put("newer", apkUrl != null && cmpVersion(latest, cur) > 0); }
                if (updError != null && !checked) o.put("error", updError);
                o.put("downloading", downloading);
                return o.toString();
            } catch (Exception e) { return "{}"; }
        }
    }

    @Override
    protected void onDestroy() {
        if (callback != null) {
            try { cm.unregisterNetworkCallback(callback); } catch (Exception ignored) { }
        }
        cm.bindProcessToNetwork(null);
        if (web != null) web.destroy();
        super.onDestroy();
    }
}
