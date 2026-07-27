package net.wargushd.game;

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
}
