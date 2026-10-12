//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file webm_movie.cpp - WebM (VP8/VP9) cinematic playback for the Remastered videos.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

/**
**  Warcraft II: Remastered ships its cinematics as **WebM** -- `Data/Movies` holds 26 files
**  (`intro.webm`, `human_act_2/3/4.webm`, `human_finale.webm`, the orc set, ...), each with a
**  sibling `.wav` carrying the audio. Stratagus's movie player is an OGG/Theora decoder and
**  cannot open them (MV1).
**
**  The Android path decodes with the platform's own hardware decoder through the NDK media API --
**  AMediaExtractor demuxes, AMediaCodec decodes onto an AImageReader surface, and the normalized
**  YUV_420_888 image is read back and converted to RGB for the existing present loop.
**
**  Audio comes from the sibling `.wav` through the normal music path, exactly as the Theora player
**  streams the movie's own audio, so no audio codec work is needed.
**
**  Anything that is not Android, or an API below 26, or a decoder that hands back an opaque/tiled
**  buffer, returns false and PlayMovie falls back to its previous behaviour.
*/

#include "stratagus.h"

#include "movie.h"

#include "debug_bridge.h"

#include "iolib.h"
#include "network.h"
#include "sound.h"
#include "sound_server.h"
#include "ui.h"
#include "video.h"

#include <string>
#include <vector>

#ifdef __ANDROID__

#include <android/api-level.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImageReader.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{

// API 26/29 entry points, resolved at runtime: the app's minSdk is lower than the API that
// introduced them, so they must not be referenced at link time.
using AImage_getHardwareBuffer_t = media_status_t (*)(const AImage *, AHardwareBuffer **);
using AHardwareBuffer_lockPlanes_t =
	int (*)(AHardwareBuffer *, uint64_t, int32_t, const ARect *, AHardwareBuffer_Planes *);
using AHardwareBuffer_unlock_t = int (*)(AHardwareBuffer *, int32_t *);
using AHardwareBuffer_describe_t = void (*)(const AHardwareBuffer *, AHardwareBuffer_Desc *);

AImage_getHardwareBuffer_t p_getHB = nullptr;
AHardwareBuffer_lockPlanes_t p_lockPlanes = nullptr;
AHardwareBuffer_unlock_t p_unlock = nullptr;
AHardwareBuffer_describe_t p_describe = nullptr;

void LoadMediaSymbols()
{
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	if (void *lib = dlopen("libmediandk.so", RTLD_NOW)) {
		p_getHB = (AImage_getHardwareBuffer_t) dlsym(lib, "AImage_getHardwareBuffer");
	}
	if (void *ah = dlopen("libandroid.so", RTLD_NOW)) {
		p_lockPlanes = (AHardwareBuffer_lockPlanes_t) dlsym(ah, "AHardwareBuffer_lockPlanes");
		p_unlock = (AHardwareBuffer_unlock_t) dlsym(ah, "AHardwareBuffer_unlock");
		p_describe = (AHardwareBuffer_describe_t) dlsym(ah, "AHardwareBuffer_describe");
	}
}

/// BT.601 studio-range YUV 4:2:0 -> packed RGB24, honouring the per-plane strides that NV12 /
/// NV21 / I420 all arrive with through the ImageReader.
bool ImageToRGB(AImage *img, std::vector<uint8_t> &rgb, int w, int h)
{
	LoadMediaSymbols();
	if (!p_getHB || !p_lockPlanes || !p_unlock) {
		return false;
	}
	AHardwareBuffer *hb = nullptr;
	if (p_getHB(img, &hb) != AMEDIA_OK || !hb) {
		return false;
	}
	AHardwareBuffer_Planes pl{};
	if (p_lockPlanes(hb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &pl) != 0) {
		return false;
	}
	const uint8_t *yb = (const uint8_t *) pl.planes[0].data;
	const int yRow = pl.planes[0].rowStride;
	const int yPix = pl.planes[0].pixelStride ? pl.planes[0].pixelStride : 1;
	if (yRow <= 0 || yPix <= 0) {
		// Opaque/tiled output (e.g. Qualcomm UBWC): the strides are meaningless and the buffer is
		// not linearly CPU-readable. Bail rather than present garbage.
		p_unlock(hb, nullptr);
		return false;
	}
	const uint8_t *ub;
	const uint8_t *vb;
	int uRow, uPix, vRow, vPix;
	if (pl.planeCount >= 3) {
		ub = (const uint8_t *) pl.planes[1].data;
		uRow = pl.planes[1].rowStride;
		uPix = pl.planes[1].pixelStride;
		vb = (const uint8_t *) pl.planes[2].data;
		vRow = pl.planes[2].rowStride;
		vPix = pl.planes[2].pixelStride;
	} else {
		// Semiplanar NV12 in one plane: interleaved UV follows Y at rowStride * bufferHeight.
		int bh = h;
		if (p_describe) {
			AHardwareBuffer_Desc dd{};
			p_describe(hb, &dd);
			if ((int) dd.height >= h) {
				bh = (int) dd.height;
			}
		}
		ub = yb + (size_t) yRow * bh;
		uRow = yRow;
		uPix = 2;
		vb = ub + 1;
		vRow = yRow;
		vPix = 2;
	}
	if (!yb || !ub || !vb) {
		p_unlock(hb, nullptr);
		return false;
	}
	rgb.resize((size_t) w * h * 3);
	uint8_t *out = rgb.data();
	for (int y = 0; y < h; ++y) {
		const uint8_t *yr = yb + (size_t) y * yRow;
		const uint8_t *ur = ub + (size_t) (y / 2) * uRow;
		const uint8_t *vr = vb + (size_t) (y / 2) * vRow;
		uint8_t *orow = out + (size_t) y * w * 3;
		for (int x = 0; x < w; ++x) {
			const int Y = yr[(size_t) x * yPix];
			const int U = ur[(size_t) (x / 2) * uPix];
			const int V = vr[(size_t) (x / 2) * vPix];
			const int c = Y - 16, d = U - 128, e = V - 128;
			const int r = (298 * c + 409 * e + 128) >> 8;
			const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			const int b = (298 * c + 516 * d + 128) >> 8;
			orow[x * 3 + 0] = (uint8_t) (r < 0 ? 0 : (r > 255 ? 255 : r));
			orow[x * 3 + 1] = (uint8_t) (g < 0 ? 0 : (g > 255 ? 255 : g));
			orow[x * 3 + 2] = (uint8_t) (b < 0 ? 0 : (b > 255 ? 255 : b));
		}
	}
	p_unlock(hb, nullptr);
	return true;
}

struct WebMDecoder
{
	AMediaExtractor *extractor = nullptr;
	AMediaCodec *codec = nullptr;
	AImageReader *reader = nullptr;
	ANativeWindow *window = nullptr;
	FILE *fp = nullptr;   // kept open: the extractor reads samples lazily
	int w = 0;
	int h = 0;
	double usPerFrame = 1000000.0 / 30.0;
	bool inputEos = false;
	bool outputEos = false;
};

void CloseDecoder(WebMDecoder *d)
{
	if (!d) {
		return;
	}
	if (d->codec) {
		AMediaCodec_stop(d->codec);
		AMediaCodec_delete(d->codec);
	}
	if (d->reader) {
		AImageReader_delete(d->reader);
	}
	if (d->extractor) {
		AMediaExtractor_delete(d->extractor);
	}
	if (d->fp) {
		fclose(d->fp);
	}
	delete d;
}

WebMDecoder *OpenDecoder(const std::string &path)
{
	if (android_get_device_api_level() < 26) {
		ErrorPrint("WebM: needs API 26+, this device is %d\n", android_get_device_api_level());
		return nullptr;
	}
	WebMDecoder *d = new WebMDecoder();
	d->fp = fopen(path.c_str(), "rb");
	if (!d->fp) {
		ErrorPrint("WebM: cannot open '%s'\n", path.c_str());
		CloseDecoder(d);
		return nullptr;
	}
	fseek(d->fp, 0, SEEK_END);
	const long size = ftell(d->fp);
	fseek(d->fp, 0, SEEK_SET);

	d->extractor = AMediaExtractor_new();
	if (AMediaExtractor_setDataSourceFd(d->extractor, fileno(d->fp), 0, (int64_t) size) != AMEDIA_OK) {
		ErrorPrint("WebM: setDataSourceFd failed for '%s'\n", path.c_str());
		CloseDecoder(d);
		return nullptr;
	}

	const size_t tracks = AMediaExtractor_getTrackCount(d->extractor);
	int videoTrack = -1;
	const char *mime = nullptr;
	AMediaFormat *vfmt = nullptr;
	for (size_t i = 0; i < tracks; ++i) {
		AMediaFormat *fmt = AMediaExtractor_getTrackFormat(d->extractor, i);
		const char *m = nullptr;
		if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m) && m
			&& strncmp(m, "video/", 6) == 0) {
			videoTrack = (int) i;
			mime = m;
			vfmt = fmt;
			break;
		}
		AMediaFormat_delete(fmt);
	}
	if (videoTrack < 0 || !vfmt) {
		ErrorPrint("WebM: no video track in '%s'\n", path.c_str());
		CloseDecoder(d);
		return nullptr;
	}
	AMediaExtractor_selectTrack(d->extractor, videoTrack);

	int32_t w = 0, h = 0, fps = 0;
	AMediaFormat_getInt32(vfmt, AMEDIAFORMAT_KEY_WIDTH, &w);
	AMediaFormat_getInt32(vfmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
	AMediaFormat_getInt32(vfmt, AMEDIAFORMAT_KEY_FRAME_RATE, &fps);
	if (w <= 0 || h <= 0) {
		AMediaFormat_delete(vfmt);
		CloseDecoder(d);
		return nullptr;
	}
	d->w = w;
	d->h = h;
	d->usPerFrame = (fps > 0) ? (1000000.0 / fps) : (1000000.0 / 30.0);

	media_status_t rs = AMEDIA_ERROR_UNKNOWN;
	{
		// The chroma planes are only CPU-mapped when the reader is created with CPU_READ_OFTEN,
		// which needs AImageReader_newWithUsage (API 26).
		using fn_t = media_status_t (*)(int32_t, int32_t, int32_t, uint64_t, int32_t, AImageReader **);
		static fn_t fn = []() -> fn_t {
			void *lib = dlopen("libmediandk.so", RTLD_NOW);
			return lib ? (fn_t) dlsym(lib, "AImageReader_newWithUsage") : nullptr;
		}();
		if (fn) {
			rs = fn(w, h, AIMAGE_FORMAT_YUV_420_888, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 6,
					&d->reader);
		} else {
			rs = AImageReader_new(w, h, AIMAGE_FORMAT_YUV_420_888, 6, &d->reader);
		}
	}
	if (rs != AMEDIA_OK || !d->reader) {
		ErrorPrint("WebM: AImageReader failed (%d)\n", (int) rs);
		AMediaFormat_delete(vfmt);
		CloseDecoder(d);
		return nullptr;
	}
	AImageReader_getWindow(d->reader, &d->window);

	d->codec = AMediaCodec_createDecoderByType(mime);
	if (!d->codec) {
		ErrorPrint("WebM: no decoder for %s\n", mime);
		AMediaFormat_delete(vfmt);
		CloseDecoder(d);
		return nullptr;
	}
	const media_status_t cs = AMediaCodec_configure(d->codec, vfmt, d->window, nullptr, 0);
	AMediaFormat_delete(vfmt);
	if (cs != AMEDIA_OK || AMediaCodec_start(d->codec) != AMEDIA_OK) {
		ErrorPrint("WebM: codec configure/start failed (%d)\n", (int) cs);
		CloseDecoder(d);
		return nullptr;
	}
	return d;
}

/// Decode the next frame into `rgb`. False at end of stream or on an unusable buffer.
bool NextFrame(WebMDecoder *d, std::vector<uint8_t> &rgb)
{
	if (!d || d->outputEos) {
		return false;
	}
	for (int guard = 0; guard < 600; ++guard) {
		if (!d->inputEos) {
			const ssize_t inIdx = AMediaCodec_dequeueInputBuffer(d->codec, 2000);
			if (inIdx >= 0) {
				size_t cap = 0;
				uint8_t *buf = AMediaCodec_getInputBuffer(d->codec, inIdx, &cap);
				const ssize_t n = AMediaExtractor_readSampleData(d->extractor, buf, cap);
				if (n < 0) {
					AMediaCodec_queueInputBuffer(d->codec, inIdx, 0, 0, 0,
												 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
					d->inputEos = true;
				} else {
					const int64_t pts = AMediaExtractor_getSampleTime(d->extractor);
					AMediaCodec_queueInputBuffer(d->codec, inIdx, 0, n, pts, 0);
					AMediaExtractor_advance(d->extractor);
				}
			}
		}
		AMediaCodecBufferInfo info{};
		const ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(d->codec, &info, 2000);
		if (outIdx >= 0) {
			const bool render = info.size > 0;
			AMediaCodec_releaseOutputBuffer(d->codec, outIdx, render);
			if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
				d->outputEos = true;
			}
			if (render) {
				AImage *img = nullptr;
				if (AImageReader_acquireLatestImage(d->reader, &img) == AMEDIA_OK && img) {
					const bool ok = ImageToRGB(img, rgb, d->w, d->h);
					AImage_delete(img);
					if (ok) {
						return true;
					}
					return false;   // opaque/tiled output: give up, caller falls back
				}
			}
			if (d->outputEos) {
				return false;
			}
		} else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER && d->inputEos) {
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
	return false;
}

} // namespace

/// Set by the skip callbacks below; local to this player so it does not share state with
/// the Theora path's own stop flag.
static bool WebMStop = false;

bool PlayWebMMovie(const std::string &filename)
{
	WebMDecoder *d = OpenDecoder(filename);
	if (!d) {
		return false;
	}

	// Letterbox exactly like the Theora player does.
	SDL_Rect rect;
	if (d->w * 300 / 4 > d->h * 100) {
		rect.w = Video.Width;
		rect.h = Video.Width * d->h / d->w;
		rect.x = 0;
		rect.y = (Video.Height - rect.h) / 2;
	} else {
		rect.w = Video.Height * d->w / d->h;
		rect.h = Video.Height;
		rect.x = (Video.Width - rect.w) / 2;
		rect.y = 0;
	}

	SDL_Texture *tex = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_RGB24,
										 SDL_TEXTUREACCESS_STREAMING, d->w, d->h);
	if (!tex) {
		ErrorPrint("WebM: SDL_CreateTexture: %s\n", SDL_GetError());
		CloseDecoder(d);
		return false;
	}

	// The Remastered keeps each cinematic's audio in a sibling .wav; play it as music at the
	// effects volume, the same way the Theora path streams the movie's own audio.
	std::string wav = filename;
	const size_t dot = wav.rfind('.');
	if (dot != std::string::npos) {
		wav = wav.substr(0, dot) + ".wav";
	}
	StopMusic();
	const int prevMusicVolume = GetMusicVolume();
	SetMusicVolume(GetEffectsVolume());
	if (CanAccessFile(wav.c_str())) {
		PlayMusic(wav);
		CallbackMusicDisable();
	}

	EventCallback callbacks;
	callbacks.ButtonPressed = [](unsigned) { WebMStop = true; };
	callbacks.ButtonReleased = [](unsigned) {};
	callbacks.MouseMoved = [](const PixelPos &) {};
	callbacks.MouseExit = []() {};
	callbacks.KeyPressed = [](unsigned, unsigned) { WebMStop = true; };
	callbacks.KeyReleased = [](unsigned, unsigned) {};
	callbacks.KeyRepeated = [](unsigned, unsigned) {};
	callbacks.NetworkEvent = NetworkEvent;
	const EventCallback *old_callbacks = GetCallbacks();
	SetCallbacks(&callbacks);

	Invalidate();
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 255);
	SDL_RenderClear(TheRenderer);

	WebMStop = false;
	std::vector<uint8_t> rgb;
	const unsigned int start = SDL_GetTicks();
	int frame = 0;
	while (!WebMStop && NextFrame(d, rgb)) {
		// Playback runs its own loop, so without this the main-loop watchdog sees the checkpoint
		// frozen for the whole clip and reports a stall of the film's length (NG83). Bumping it per
		// presented frame keeps the watchdog meaningful during a movie: a real wedge stops the
		// frames, and so stops the heartbeat.
		DBG_CHECKPOINT("movie:webm");
		SDL_UpdateTexture(tex, nullptr, rgb.data(), d->w * 3);
		SDL_RenderClear(TheRenderer);
		SDL_RenderCopy(TheRenderer, tex, nullptr, &rect);
		SDL_RenderPresent(TheRenderer);
		++frame;

		// Schedule against the clip's own frame rate; a late frame is simply presented at once.
		const int due = (int) (frame * d->usPerFrame / 1000.0);
		const int now = (int) (SDL_GetTicks() - start);
		if (due > now) {
			SDL_Delay(due - now);
		}
		WaitEventsOneFrame();
	}

	StopMusic();
	SetMusicVolume(prevMusicVolume);
	CallbackMusicEnable();
	SDL_DestroyTexture(tex);
	CloseDecoder(d);
	SetCallbacks(old_callbacks);
	return frame > 0;
}

#else // !__ANDROID__

bool PlayWebMMovie(const std::string &)
{
	// The software VP8/VP9 path is not built on desktop yet; PlayMovie falls back.
	return false;
}

#endif
