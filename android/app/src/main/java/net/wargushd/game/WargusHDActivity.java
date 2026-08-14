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
    // Wi-Fi without it. The lock is held only while the activity is in the
    // foreground so battery is not wasted when the game is backgrounded. The
    // in-engine multiplayer lobby and protocol are unchanged; this only makes
    // their existing network traffic reach the device. (A high-performance
    // WifiLock would additionally need the WAKE_LOCK permission and is not
    // required for discovery, so it is deliberately not used.)
    private WifiManager.MulticastLock multicastLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final WifiManager wifi =
            (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("wargushd-lan");
            multicastLock.setReferenceCounted(false);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (multicastLock != null && !multicastLock.isHeld()) {
            multicastLock.acquire();
        }
    }

    @Override
    protected void onPause() {
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        super.onPause();
    }
}
