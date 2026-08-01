#ifndef __SHADERS_H__
#define __SHADERS_H__

#include <SDL.h>

#ifndef __APPLE__
extern bool LoadShaderExtensions();
extern bool RenderWithShader(SDL_Renderer *renderer, SDL_Window* win, SDL_Texture* backBuffer);
// True when a post-process shader will consume the overlay this frame (RenderWithShader takes its
// path). The overlay upload can't be skipped then, as the shader reads the whole backbuffer.
extern bool IsShaderActive();
#else
#include "stratagus.h"

inline bool LoadShaderExtensions() {
    return false;
}

inline bool RenderWithShader(SDL_Renderer*, SDL_Window*, SDL_Texture*) {
    return false;
}

inline bool IsShaderActive() {
    return false;
}
#endif

#endif
