package net.wargushd.game;

import android.net.wifi.WifiManager;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/**
 * Entry activity for the Android build. The whole engine (including SDL2) is
 * linked statically into a single libmain.so, so only "main" is loaded; SDL's
 * default {"SDL2", "main"} would try to dlopen a separate libSDL2.so that does
 * not exist here.
 */
public class WargusHDActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "main"
        };
    }

    // LAN multiplayer plumbing. Android does not deliver multicast/broadcast
    // datagrams to an app unless it holds a WifiManager MulticastLock, so the
    // engine's own mDNS LAN host discovery (mdns_wrapper) cannot see peers on the
    // Wi-Fi without it. A high-performance WifiLock keeps the radio from dozing so
    // the lockstep net game is not throttled while the screen is on. Both are held
    // only while the activity is in the foreground so battery is not wasted when
    // the game is backgrounded. The in-engine multiplayer lobby and protocol are
    // unchanged; this only makes their existing network traffic reach the device.
    private WifiManager.MulticastLock multicastLock;
    private WifiManager.WifiLock wifiLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final WifiManager wifi =
            (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("wargushd-lan");
            multicastLock.setReferenceCounted(false);
            wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "wargushd-lan");
            wifiLock.setReferenceCounted(false);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (multicastLock != null && !multicastLock.isHeld()) {
            multicastLock.acquire();
        }
        if (wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
        }
    }

    @Override
    protected void onPause() {
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        if (wifiLock != null && wifiLock.isHeld()) {
            wifiLock.release();
        }
        super.onPause();
    }
}
