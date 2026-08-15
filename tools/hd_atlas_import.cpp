//     _________ __                 __
//    /   _____//  |_____________ _/  |______     ____  __ __  ______
//    \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//    /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//   /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//           \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file hd_atlas_import.cpp - Host tool: TexturePacker atlas -> .hdatlas + pages.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

// Offline importer that converts a Warcraft II Remastered TexturePacker atlas
// (JSON Hash + RGBA PNG) into a compact runtime binary (.hdatlas) plus one or
// more GPU-cappable RGBA PNG pages. It repacks every sprite rect into fresh
// pages no larger than a GPU size cap, sharing pixels between frames that point
// at an identical source rect. Nothing renders here; the engine consumer and
// the doctest suite are separate tasks.

#include "hd_atlas.h"
#include "json.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <png.h>

namespace
{

// ---------------------------------------------------------------------------
// In-memory RGBA8888 image (row-major, top-down, 4 bytes/pixel).
// ---------------------------------------------------------------------------
struct Image {
	int w = 0;
	int h = 0;
	std::vector<uint8_t> px; // w*h*4

	Image() = default;
	Image(int width, int height) : w(width), h(height), px(size_t(width) * height * 4, 0) {}

	uint8_t *row(int y) { return px.data() + size_t(y) * w * 4; }
	const uint8_t *row(int y) const { return px.data() + size_t(y) * w * 4; }
};

// ---------------------------------------------------------------------------
// Parsed TexturePacker frame (source-atlas coordinates).
// ---------------------------------------------------------------------------
struct FrameInput {
	std::string name;
	int fx = 0, fy = 0, fw = 0, fh = 0; // frame rect in the source atlas PNG
	int offx = 0, offy = 0;             // spriteSourceSize.x / .y
	int cw = 0, ch = 0;                 // sourceSize.w / .h
	int rotated = 0;
};

// ---------------------------------------------------------------------------
// Little-endian byte emitters for the .hdatlas stream.
// ---------------------------------------------------------------------------
void putU16(std::vector<uint8_t> &b, uint16_t v)
{
	b.push_back(uint8_t(v & 0xFF));
	b.push_back(uint8_t((v >> 8) & 0xFF));
}
void putU32(std::vector<uint8_t> &b, uint32_t v)
{
	b.push_back(uint8_t(v & 0xFF));
	b.push_back(uint8_t((v >> 8) & 0xFF));
	b.push_back(uint8_t((v >> 16) & 0xFF));
	b.push_back(uint8_t((v >> 24) & 0xFF));
}
void putI16(std::vector<uint8_t> &b, int16_t v) { putU16(b, uint16_t(v)); }

// ---------------------------------------------------------------------------
// PNG load: normalize any input to 8-bit RGBA, pixels copied verbatim.
// ---------------------------------------------------------------------------
bool loadPng(const std::string &path, Image &out, std::string &err)
{
	FILE *fp = fopen(path.c_str(), "rb");
	if (!fp) {
		err = "cannot open PNG: " + path;
		return false;
	}
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png) {
		fclose(fp);
		err = "png_create_read_struct failed";
		return false;
	}
	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, nullptr, nullptr);
		fclose(fp);
		err = "png_create_info_struct failed";
		return false;
	}
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, nullptr);
		fclose(fp);
		err = "libpng error while reading: " + path;
		return false;
	}
	png_init_io(png, fp);
	png_read_info(png, info);

	png_uint_32 w = 0, h = 0;
	int bitDepth = 0, colorType = 0, interlace = 0, comp = 0, filter = 0;
	png_get_IHDR(png, info, &w, &h, &bitDepth, &colorType, &interlace, &comp, &filter);

	// Normalize to 8-bit RGBA regardless of the source encoding.
	if (colorType == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png);
	}
	if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
		png_set_expand_gray_1_2_4_to_8(png);
	}
	if (png_get_valid(png, info, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png);
	}
	if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png);
	}
	if (bitDepth == 16) {
		png_set_strip_16(png);
	}
	// Add an opaque alpha channel if the image has none.
	png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	if (interlace != PNG_INTERLACE_NONE) {
		png_set_interlace_handling(png);
	}
	png_read_update_info(png, info);

	out.w = int(w);
	out.h = int(h);
	out.px.assign(size_t(w) * h * 4, 0);
	std::vector<png_bytep> rows(h);
	for (png_uint_32 y = 0; y < h; ++y) {
		rows[y] = out.px.data() + size_t(y) * w * 4;
	}
	png_read_image(png, rows.data());
	png_read_end(png, nullptr);
	png_destroy_read_struct(&png, &info, nullptr);
	fclose(fp);
	return true;
}

// ---------------------------------------------------------------------------
// PNG save: 8-bit RGBA, no palette. Emits the top-left usedW x usedH region.
// ---------------------------------------------------------------------------
bool savePng(const std::string &path, const Image &img, int usedW, int usedH, std::string &err)
{
	FILE *fp = fopen(path.c_str(), "wb");
	if (!fp) {
		err = "cannot open PNG for write: " + path;
		return false;
	}
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png) {
		fclose(fp);
		err = "png_create_write_struct failed";
		return false;
	}
	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_write_struct(&png, nullptr);
		fclose(fp);
		err = "png_create_info_struct failed";
		return false;
	}
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(fp);
		err = "libpng error while writing: " + path;
		return false;
	}
	png_init_io(png, fp);
	png_set_IHDR(png, info, png_uint_32(usedW), png_uint_32(usedH), 8, PNG_COLOR_TYPE_RGBA,
	             PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	png_write_info(png, info);
	std::vector<png_bytep> rows(usedH);
	for (int y = 0; y < usedH; ++y) {
		// Row pointers reference the sub-region of the (wider) page buffer.
		rows[y] = const_cast<png_bytep>(img.row(y));
	}
	png_write_image(png, rows.data());
	png_write_end(png, info);
	png_destroy_write_struct(&png, &info);
	fclose(fp);
	return true;
}

// ---------------------------------------------------------------------------
// TexturePacker JSON ("JSON Hash") -> FrameInput list + atlas dimensions.
// ---------------------------------------------------------------------------
bool parseAtlas(const std::string &text, std::vector<FrameInput> &frames, int &metaW, int &metaH,
                std::string &err)
{
	content::JsonValue root;
	std::string jerr;
	if (!content::parseJson(text, root, jerr)) {
		err = "JSON parse error: " + jerr;
		return false;
	}
	if (!root.isObject()) {
		err = "atlas root is not an object";
		return false;
	}
	const content::JsonValue *framesNode = root.find("frames");
	if (!framesNode || !framesNode->isObject() || !framesNode->object) {
		err = "atlas has no 'frames' object";
		return false;
	}
	const content::JsonValue *meta = root.find("meta");
	metaW = metaH = 0;
	if (meta) {
		const content::JsonValue *size = meta->find("size");
		if (size) {
			metaW = int(size->find("w") ? size->find("w")->asInt(0) : 0);
			metaH = int(size->find("h") ? size->find("h")->asInt(0) : 0);
		}
	}

	auto req = [](const content::JsonValue *obj, const char *key, int &dst) -> bool {
		if (!obj) {
			return false;
		}
		const content::JsonValue *v = obj->find(key);
		if (!v || !v->isNumber()) {
			return false;
		}
		dst = int(v->asInt(0));
		return true;
	};

	for (const auto &kv : *framesNode->object) {
		const std::string &name = kv.first;
		const content::JsonValue &f = kv.second;
		if (!f.isObject()) {
			err = "frame '" + name + "' is not an object";
			return false;
		}
		FrameInput fi;
		fi.name = name;

		const content::JsonValue *frameRect = f.find("frame");
		if (!req(frameRect, "x", fi.fx) || !req(frameRect, "y", fi.fy) ||
		    !req(frameRect, "w", fi.fw) || !req(frameRect, "h", fi.fh)) {
			err = "frame '" + name + "' missing valid 'frame' rect";
			return false;
		}
		const content::JsonValue *rot = f.find("rotated");
		if (rot && !rot->isBool()) {
			err = "frame '" + name + "' has a non-boolean 'rotated'";
			return false;
		}
		fi.rotated = (rot && rot->asBool(false)) ? 1 : 0;
		if (fi.rotated) {
			err = "frame '" + name + "' is rotated; unsupported in P0";
			return false;
		}

		const content::JsonValue *sss = f.find("spriteSourceSize");
		if (sss) {
			// When present, both x and y must be numeric; a partial/malformed
			// spriteSourceSize would silently corrupt trimmed-sprite placement.
			if (!req(sss, "x", fi.offx) || !req(sss, "y", fi.offy)) {
				err = "frame '" + name + "' has an invalid 'spriteSourceSize' (x/y must be numbers)";
				return false;
			}
		}
		const content::JsonValue *ss = f.find("sourceSize");
		if (!req(ss, "w", fi.cw) || !req(ss, "h", fi.ch)) {
			// Fall back to the frame dimensions when sourceSize is absent.
			fi.cw = fi.fw;
			fi.ch = fi.fh;
		}
		frames.push_back(fi);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Shelf/skyline bin-packer. Repacks each unique source rect into pages of size
// <= cap, sharing pixels between duplicate rects. Gutter is 0 px so a packed
// rect is a byte-exact copy of its source rect. Fills records (sorted by name)
// and pages (each allocated cap x cap; usedW/usedH give the emitted crop).
// ---------------------------------------------------------------------------
bool packAtlas(std::vector<FrameInput> &frames, const Image &src, int cap,
               std::vector<hdatlas::HdFrameRecord> &records, std::vector<Image> &pages,
               std::vector<int> &usedW, std::vector<int> &usedH, std::string &err)
{
	// Deterministic order: sort by frame name (std::string operator<).
	std::sort(frames.begin(), frames.end(),
	          [](const FrameInput &a, const FrameInput &b) { return a.name < b.name; });

	// Packed location for a unique source rect.
	struct Loc {
		int page, x, y;
	};
	std::map<std::array<int, 4>, Loc> placed; // (fx,fy,fw,fh) -> location

	int curPage = -1;
	int shelfX = 0, shelfY = 0, shelfH = 0;

	auto newPage = [&]() {
		pages.emplace_back(cap, cap);
		usedW.push_back(0);
		usedH.push_back(0);
		curPage = int(pages.size()) - 1;
		shelfX = 0;
		shelfY = 0;
		shelfH = 0;
	};

	records.reserve(frames.size());
	for (const FrameInput &fi : frames) {
		const int w = fi.fw, h = fi.fh;
		if (w > cap || h > cap) {
			char buf[256];
			std::snprintf(buf, sizeof(buf),
			              "frame '%s' (%dx%d) exceeds the page cap (%d)", fi.name.c_str(), w, h,
			              cap);
			err = buf;
			return false;
		}
		std::array<int, 4> key = {fi.fx, fi.fy, w, h};
		Loc loc;
		auto it = placed.find(key);
		if (it != placed.end()) {
			// Duplicate source rect: reuse the already-packed pixels.
			loc = it->second;
		} else {
			if (curPage < 0) {
				newPage();
			}
			// Advance to a new shelf if the current row is full.
			if (shelfX + w > cap) {
				shelfY += shelfH;
				shelfX = 0;
				shelfH = 0;
			}
			// Advance to a new page if the current page is full.
			if (shelfY + h > cap) {
				newPage();
			}
			loc.page = curPage;
			loc.x = shelfX;
			loc.y = shelfY;
			// Copy the sw x sh rect verbatim (RGBA, row by row).
			Image &pg = pages[curPage];
			for (int r = 0; r < h; ++r) {
				const uint8_t *s = src.row(fi.fy + r) + size_t(fi.fx) * 4;
				uint8_t *d = pg.row(loc.y + r) + size_t(loc.x) * 4;
				std::memcpy(d, s, size_t(w) * 4);
			}
			shelfX += w;
			if (h > shelfH) {
				shelfH = h;
			}
			if (loc.x + w > usedW[curPage]) {
				usedW[curPage] = loc.x + w;
			}
			if (loc.y + h > usedH[curPage]) {
				usedH[curPage] = loc.y + h;
			}
			placed.emplace(key, loc);
		}

		hdatlas::HdFrameRecord rec;
		std::memset(&rec, 0, sizeof(rec));
		rec.nameHash = hdatlas::fnv1a(fi.name.c_str());
		rec.page = uint16_t(loc.page);
		rec.sx = uint16_t(loc.x);
		rec.sy = uint16_t(loc.y);
		rec.sw = uint16_t(w);
		rec.sh = uint16_t(h);
		rec.offx = int16_t(fi.offx);
		rec.offy = int16_t(fi.offy);
		rec.cw = uint16_t(fi.cw);
		rec.ch = uint16_t(fi.ch);
		rec.rotated = 0;
		rec._pad = 0;
		records.push_back(rec);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Serialize the .hdatlas stream (see hd_atlas.h for the layout).
// ---------------------------------------------------------------------------
void writeAtlasBytes(uint32_t cap, const std::vector<std::string> &pageNames,
                     const std::vector<hdatlas::HdFrameRecord> &records, std::vector<uint8_t> &out)
{
	out.insert(out.end(), hdatlas::kMagic, hdatlas::kMagic + 4);
	putU32(out, cap);
	putU32(out, uint32_t(pageNames.size()));
	putU32(out, uint32_t(records.size()));
	for (const std::string &n : pageNames) {
		putU16(out, uint16_t(n.size()));
		out.insert(out.end(), n.begin(), n.end());
	}
	for (const hdatlas::HdFrameRecord &r : records) {
		putU32(out, r.nameHash);
		putU16(out, r.page);
		putU16(out, r.sx);
		putU16(out, r.sy);
		putU16(out, r.sw);
		putU16(out, r.sh);
		putI16(out, r.offx);
		putI16(out, r.offy);
		putU16(out, r.cw);
		putU16(out, r.ch);
		out.push_back(r.rotated);
		out.push_back(r._pad);
	}
}

std::string baseName(const std::string &path)
{
	size_t p = path.find_last_of("/\\");
	return p == std::string::npos ? path : path.substr(p + 1);
}

bool readFile(const std::string &path, std::string &out, std::string &err)
{
	FILE *fp = fopen(path.c_str(), "rb");
	if (!fp) {
		err = "cannot open: " + path;
		return false;
	}
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (n < 0) {
		fclose(fp);
		err = "cannot size: " + path;
		return false;
	}
	out.resize(size_t(n));
	size_t got = n ? fread(&out[0], 1, size_t(n), fp) : 0;
	fclose(fp);
	if (got != size_t(n)) {
		err = "short read: " + path;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Self-test: synthesize a tiny atlas in memory and run the full pipeline.
// ---------------------------------------------------------------------------
int runSelfTest()
{
	// Deterministic 16x16 RGBA source; each pixel encodes its coordinates so a
	// verbatim crop is easy to verify.
	Image src(16, 16);
	for (int y = 0; y < src.h; ++y) {
		for (int x = 0; x < src.w; ++x) {
			uint8_t *p = src.row(y) + size_t(x) * 4;
			p[0] = uint8_t(x * 16);
			p[1] = uint8_t(y * 16);
			p[2] = uint8_t((x + y) * 8);
			p[3] = 0xFF;
		}
	}

	// 4 frames: a_0 and a_1 share a source rect (duplicate); b_trim is trimmed.
	const std::string json =
	    "{ \"frames\": {"
	    "  \"a_0\": { \"frame\": {\"x\":0,\"y\":0,\"w\":4,\"h\":4}, \"rotated\": false,"
	    "             \"trimmed\": false,"
	    "             \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":4,\"h\":4},"
	    "             \"sourceSize\": {\"w\":4,\"h\":4} },"
	    "  \"a_1\": { \"frame\": {\"x\":0,\"y\":0,\"w\":4,\"h\":4}, \"rotated\": false,"
	    "             \"trimmed\": false,"
	    "             \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":4,\"h\":4},"
	    "             \"sourceSize\": {\"w\":4,\"h\":4} },"
	    "  \"b_trim\": { \"frame\": {\"x\":4,\"y\":0,\"w\":6,\"h\":3}, \"rotated\": false,"
	    "                \"trimmed\": true,"
	    "                \"spriteSourceSize\": {\"x\":2,\"y\":1,\"w\":6,\"h\":3},"
	    "                \"sourceSize\": {\"w\":10,\"h\":8} },"
	    "  \"c_0\": { \"frame\": {\"x\":0,\"y\":4,\"w\":5,\"h\":5}, \"rotated\": false,"
	    "             \"trimmed\": false,"
	    "             \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":5,\"h\":5},"
	    "             \"sourceSize\": {\"w\":5,\"h\":5} } }"
	    ", \"meta\": { \"size\": {\"w\":16,\"h\":16}, \"format\": \"RGBA8888\" } }";

	std::string err;
	std::vector<FrameInput> frames;
	int metaW = 0, metaH = 0;
	if (!parseAtlas(json, frames, metaW, metaH, err)) {
		fprintf(stderr, "SELFTEST FAIL: parse: %s\n", err.c_str());
		return 1;
	}
	if (metaW != 16 || metaH != 16) {
		fprintf(stderr, "SELFTEST FAIL: meta size %dx%d != 16x16\n", metaW, metaH);
		return 1;
	}

	const int cap = 16;
	std::vector<hdatlas::HdFrameRecord> records;
	std::vector<Image> pages;
	std::vector<int> usedW, usedH;
	if (!packAtlas(frames, src, cap, records, pages, usedW, usedH, err)) {
		fprintf(stderr, "SELFTEST FAIL: pack: %s\n", err.c_str());
		return 1;
	}

	// (1) record count matches frame count.
	if (records.size() != 4 || frames.size() != 4) {
		fprintf(stderr, "SELFTEST FAIL: recordCount %zu != 4\n", records.size());
		return 1;
	}

	// (2) every record fits inside its page.
	for (size_t i = 0; i < records.size(); ++i) {
		const hdatlas::HdFrameRecord &r = records[i];
		const int pw = usedW[r.page], ph = usedH[r.page];
		if (r.sx + r.sw > pw || r.sy + r.sh > ph) {
			fprintf(stderr, "SELFTEST FAIL: record %zu out of page bounds\n", i);
			return 1;
		}
	}

	// (3) each record's rect/offset/canvas equals the (sorted) input JSON values,
	//     and (4) a repacked frame's pixels byte-match the source rect.
	for (size_t i = 0; i < records.size(); ++i) {
		const FrameInput &fi = frames[i]; // frames were sorted in place by packAtlas
		const hdatlas::HdFrameRecord &r = records[i];
		if (r.nameHash != hdatlas::fnv1a(fi.name.c_str())) {
			fprintf(stderr, "SELFTEST FAIL: %s hash mismatch\n", fi.name.c_str());
			return 1;
		}
		if (r.sw != fi.fw || r.sh != fi.fh || r.offx != fi.offx || r.offy != fi.offy ||
		    r.cw != fi.cw || r.ch != fi.ch) {
			fprintf(stderr, "SELFTEST FAIL: %s field mismatch\n", fi.name.c_str());
			return 1;
		}
		const Image &pg = pages[r.page];
		for (int y = 0; y < r.sh; ++y) {
			const uint8_t *s = src.row(fi.fy + y) + size_t(fi.fx) * 4;
			const uint8_t *d = pg.row(r.sy + y) + size_t(r.sx) * 4;
			if (std::memcmp(s, d, size_t(r.sw) * 4) != 0) {
				fprintf(stderr, "SELFTEST FAIL: %s pixel mismatch at row %d\n",
				        fi.name.c_str(), y);
				return 1;
			}
		}
	}

	// (5) duplicate rects (a_0, a_1) share one packed location.
	const hdatlas::HdFrameRecord *ra = nullptr, *rb = nullptr;
	for (size_t i = 0; i < records.size(); ++i) {
		if (frames[i].name == "a_0") ra = &records[i];
		if (frames[i].name == "a_1") rb = &records[i];
	}
	if (!ra || !rb || ra->page != rb->page || ra->sx != rb->sx || ra->sy != rb->sy) {
		fprintf(stderr, "SELFTEST FAIL: duplicate rect not shared\n");
		return 1;
	}

	// Exercise serialization too (header magic + counts).
	std::vector<std::string> pageNames;
	for (size_t i = 0; i < pages.size(); ++i) {
		pageNames.push_back("selftest.p" + std::to_string(i) + ".png");
	}
	std::vector<uint8_t> blob;
	writeAtlasBytes(uint32_t(cap), pageNames, records, blob);
	if (blob.size() < 16 || std::memcmp(blob.data(), hdatlas::kMagic, 4) != 0) {
		fprintf(stderr, "SELFTEST FAIL: bad .hdatlas header\n");
		return 1;
	}

	printf("SELFTEST OK: %zu frames, %zu page(s), cap %d, %zu unique rect(s), .hdatlas %zu bytes\n",
	       records.size(), pages.size(), cap, records.size() - 1, blob.size());
	return 0;
}

void usage(const char *argv0)
{
	fprintf(stderr,
	        "Usage: %s --json <atlas.json> --png <atlas.png> --out <basename> [--cap N]\n"
	        "       %s --selftest\n",
	        argv0, argv0);
}

} // namespace

int main(int argc, char *argv[])
{
	std::string jsonPath, pngPath, outBase;
	int cap = 4096;
	bool selftest = false;

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&](const char *what) -> std::string {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: %s expects an argument\n", what);
				exit(2);
			}
			return argv[++i];
		};
		if (a == "--selftest") {
			selftest = true;
		} else if (a == "--json") {
			jsonPath = next("--json");
		} else if (a == "--png") {
			pngPath = next("--png");
		} else if (a == "--out") {
			outBase = next("--out");
		} else if (a == "--cap") {
			cap = atoi(next("--cap").c_str());
		} else if (a == "-h" || a == "--help") {
			usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "error: unknown argument '%s'\n", a.c_str());
			usage(argv[0]);
			return 2;
		}
	}

	if (selftest) {
		return runSelfTest();
	}

	if (jsonPath.empty() || outBase.empty()) {
		usage(argv[0]);
		return 2;
	}
	if (cap < 1 || cap > 8192) {
		fprintf(stderr, "error: --cap must be in [1, 8192] (got %d)\n", cap);
		return 2;
	}
	// Fallback: derive the PNG path from the JSON path (.json -> .png).
	if (pngPath.empty()) {
		pngPath = jsonPath;
		size_t dot = pngPath.rfind(".json");
		if (dot != std::string::npos && dot == pngPath.size() - 5) {
			pngPath = pngPath.substr(0, dot) + ".png";
		} else {
			pngPath += ".png";
		}
	}

	std::string err;
	std::string jsonText;
	if (!readFile(jsonPath, jsonText, err)) {
		fprintf(stderr, "error: %s\n", err.c_str());
		return 1;
	}
	std::vector<FrameInput> frames;
	int metaW = 0, metaH = 0;
	if (!parseAtlas(jsonText, frames, metaW, metaH, err)) {
		fprintf(stderr, "error: %s\n", err.c_str());
		return 1;
	}
	if (frames.empty()) {
		fprintf(stderr, "error: atlas has no frames\n");
		return 1;
	}

	Image src;
	if (!loadPng(pngPath, src, err)) {
		fprintf(stderr, "error: %s\n", err.c_str());
		return 1;
	}
	if (metaW && metaH && (src.w != metaW || src.h != metaH)) {
		fprintf(stderr, "error: PNG %dx%d does not match meta.size %dx%d\n", src.w, src.h, metaW,
		        metaH);
		return 1;
	}

	std::vector<hdatlas::HdFrameRecord> records;
	std::vector<Image> pages;
	std::vector<int> usedW, usedH;
	if (!packAtlas(frames, src, cap, records, pages, usedW, usedH, err)) {
		fprintf(stderr, "error: %s\n", err.c_str());
		return 1;
	}

	// Emit pages and collect their basenames for the .hdatlas.
	std::string outDir;
	{
		size_t p = outBase.find_last_of("/\\");
		outDir = p == std::string::npos ? std::string() : outBase.substr(0, p + 1);
	}
	std::string outLeaf = baseName(outBase);
	std::vector<std::string> pageNames;
	for (size_t i = 0; i < pages.size(); ++i) {
		std::string leaf = outLeaf + ".p" + std::to_string(i) + ".png";
		std::string full = outDir + leaf;
		if (!savePng(full, pages[i], usedW[i], usedH[i], err)) {
			fprintf(stderr, "error: %s\n", err.c_str());
			return 1;
		}
		pageNames.push_back(leaf);
	}

	std::vector<uint8_t> blob;
	writeAtlasBytes(uint32_t(cap), pageNames, records, blob);
	std::string atlasPath = outBase + ".hdatlas";
	FILE *fp = fopen(atlasPath.c_str(), "wb");
	if (!fp) {
		fprintf(stderr, "error: cannot write %s\n", atlasPath.c_str());
		return 1;
	}
	if (!blob.empty()) {
		fwrite(blob.data(), 1, blob.size(), fp);
	}
	fclose(fp);

	printf("hd_atlas_import: %zu frames, %zu page(s), cap %d -> %s\n", records.size(), pages.size(),
	       cap, atlasPath.c_str());
	return 0;
}
