//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name iolib.cpp - Compression-IO helper functions. */
//
//      (c) Copyright 2000-2011 by Andreas Arens, Lutz Sammer Jimmy Salmon and
//                                 Pali Rohár
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "iolib.h"

#include "game.h"
#include "map.h"
#include "parameters.h"
#include "script.h"
#include "stratagus.h"
#include "util.h"
#include "pud.h"

#include <SDL.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

#ifdef USE_ZLIB
#include <zlib.h>
#endif

#ifdef USE_BZ2LIB
#include <bzlib.h>
#endif

enum class ClfType
{
	Invalid, /// invalid file handle
	Plain, /// plain text file handle
	Gzip, /// gzip file handle
	Bzip2 /// bzip2 file handle
};

class CFile::PImpl
{
public:
	PImpl() = default;
	~PImpl();
	PImpl(const PImpl &) = delete;
	const PImpl &operator=(const PImpl &) = delete;

	int open(const char *name, long flags);
	int close();
	void flush();
	int read(void *buf, size_t len);
	int seek(long offset, int whence);
	long tell();
	int write(const void *buf, size_t len);

private:
	ClfType cl_type = ClfType::Invalid; /// type of CFile
	FILE *cl_plain = nullptr;  /// standard file pointer
#ifdef USE_ZLIB
	gzFile cl_gz;    /// gzip file pointer
#endif // !USE_ZLIB
#ifdef USE_BZ2LIB
	BZFILE *cl_bz = nullptr; /// bzip2 file pointer
#endif // !USE_BZ2LIB
};

CFile::CFile() : pimpl(std::make_unique<CFile::PImpl>())
{
}

CFile::~CFile() = default;

/**
**  CLopen Library file open
**
**  @param name       File name.
**  @param openflags  Open read, or write and compression options
**
**  @return File Pointer
*/
int CFile::open(const char *name, long flags)
{
	return pimpl->open(name, flags);
}

/**
**  CLclose Library file close
*/
int CFile::close()
{
	return pimpl->close();
}

void CFile::flush()
{
	pimpl->flush();
}

/**
**  CLread Library file read
**
**  @param buf  Pointer to read the data to.
**  @param len  number of bytes to read.
*/
int CFile::read(void *buf, size_t len)
{
	return pimpl->read(buf, len);
}

/**
**  CLseek Library file seek
**
**  @param offset  Seek position
**  @param whence  How to seek
*/
int CFile::seek(long offset, int whence)
{
	return pimpl->seek(offset, whence);
}

/**
**  CLtell Library file tell
*/
long CFile::tell()
{
	return pimpl->tell();
}

/**
**  CLprintf Library file write
**
**  @param data  String to write.
*/
void CFile::write(std::string_view data)
{
	pimpl->write(data.data(), data.size());
}

static Sint64 sdl_size(SDL_RWops *context)
{
	CFile *self = reinterpret_cast<CFile*>(context->hidden.unknown.data1);
	long currentPosition = self->tell();
	self->seek(0, SEEK_END);
	long size = self->tell();
	self->seek(currentPosition, SEEK_SET);
	return size;
}

static Sint64 sdl_seek(SDL_RWops *context, Sint64 offset, int whence)
{
	CFile *self = reinterpret_cast<CFile*>(context->hidden.unknown.data1);
	return self->seek(offset, whence);
}

static size_t sdl_read(SDL_RWops *context, void *ptr, size_t size, size_t maxnum)
{
	// SDL_RWread permits a zero element size; guard the division so a zero-sized
	// read from an asset loader cannot divide by zero.
	if (size == 0) {
		return 0;
	}
	CFile *self = reinterpret_cast<CFile*>(context->hidden.unknown.data1);
	return self->read(ptr, size * maxnum) / size;
}

static size_t sdl_write(SDL_RWops *context, const void *ptr, size_t size, size_t num)
{
	// Should not be called.
	return 0;
}

static int sdl_close(SDL_RWops *context)
{
	std::unique_ptr<CFile> self{reinterpret_cast<CFile *>(context->hidden.unknown.data1)};
	const int res = self->close();
	SDL_FreeRW(context);
	return res;
}

SDL_RWops *CFile::to_SDL_RWops(std::unique_ptr<CFile> file)
{
	SDL_RWops *ops = SDL_AllocRW();
	ops->type = SDL_RWOPS_UNKNOWN;
	ops->hidden.unknown.data1 = file.release();
	ops->size = sdl_size;
	ops->seek = sdl_seek;
	ops->read = sdl_read;
	ops->write = sdl_write;
	ops->close = sdl_close;
	return ops;
}

//
//  Implementation.
//

CFile::PImpl::~PImpl()
{
	if (cl_type != ClfType::Invalid) {
		DebugPrint("File wasn't closed\n");
		close();
	}
}

#ifdef USE_ZLIB

#ifndef z_off_t // { ZLIB_VERSION<="1.0.4"

/**
**  Seek on compressed input. (Newer libs support it directly)
**
**  @param file    File
**  @param offset  Seek position
**  @param whence  How to seek
*/
static int gzseek(CFile *file, unsigned offset, int whence)
{
	char buf[32];

	while (offset > sizeof(buf)) {
		gzread(file, buf, sizeof(buf));
		offset -= sizeof(buf);
	}
	return gzread(file, buf, offset);
}

#endif // } ZLIB_VERSION<="1.0.4"

#endif // USE_ZLIB

#ifdef USE_BZ2LIB

/**
**  Seek on compressed input. (I hope newer libs support it directly)
**
**  @param file    File handle
**  @param offset  Seek position
**  @param whence  How to seek
*/
static void bzseek(BZFILE *file, unsigned offset, int)
{
	char buf[32];

	while (offset > sizeof(buf)) {
		BZ2_bzread(file, buf, sizeof(buf));
		offset -= sizeof(buf);
	}
	BZ2_bzread(file, buf, offset);
}

#endif // USE_BZ2LIB

int CFile::PImpl::open(const char *name, long openflags)
{
	const char *openstring;

	if ((openflags & CL_OPEN_READ) && (openflags & CL_OPEN_WRITE)) {
		openstring = "rwb";
	} else if (openflags & CL_OPEN_READ) {
		openstring = "rb";
	} else if (openflags & CL_OPEN_WRITE) {
		openstring = "wb";
	} else {
		ErrorPrint("Bad CLopen flags when opening \"%s\"\n", name);
		Assert(0);
		return -1;
	}

	cl_type = ClfType::Invalid;

	if (openflags & CL_OPEN_WRITE) {
#ifdef USE_BZ2LIB
		if ((openflags & CL_WRITE_BZ2)
			&& (cl_bz = BZ2_bzopen((std::string(name) + ".bz2").c_str(), openstring))) {
			cl_type = ClfType::Bzip2;
		} else
#endif
#ifdef USE_ZLIB
			if ((openflags & CL_WRITE_GZ)
		        && (cl_gz = gzopen((std::string(name) + ".gz").c_str(), openstring)))
		{
				cl_type = ClfType::Gzip;
			} else
#endif
				if ((cl_plain = fopen(name, openstring))) {
					cl_type = ClfType::Plain;
				}
	} else {
		if (!(cl_plain = fopen(name, openstring))) { // try plain first
#ifdef USE_ZLIB
			if ((cl_gz = gzopen((std::string(name) + ".gz").c_str(), "rb"))) {
				cl_type = ClfType::Gzip;
			} else
#endif
#ifdef USE_BZ2LIB
				if ((cl_bz = BZ2_bzopen((std::string(name) + ".bz2").c_str(), "rb"))) {
					cl_type = ClfType::Bzip2;
				} else
#endif
				{ }

		} else {
			char buf[512];
			cl_type = ClfType::Plain;
			// Hmm, plain worked, but nevertheless the file may be compressed!
			if (fread(buf, 2, 1, cl_plain) == 1) {
#ifdef USE_BZ2LIB
				if (buf[0] == 'B' && buf[1] == 'Z') {
					fclose(cl_plain);
					if ((cl_bz = BZ2_bzopen(name, "rb"))) {
						cl_type = ClfType::Bzip2;
					} else {
						if (!(cl_plain = fopen(name, "rb"))) {
							cl_type = ClfType::Invalid;
						}
					}
				}
#endif // USE_BZ2LIB
#ifdef USE_ZLIB
				if (buf[0] == 0x1f) { // don't check for buf[1] == 0x8b, so that old compress also works!
					fclose(cl_plain);
					if ((cl_gz = gzopen(name, "rb"))) {
						cl_type = ClfType::Gzip;
					} else {
						if (!(cl_plain = fopen(name, "rb"))) {
							cl_type = ClfType::Invalid;
						}
					}
				}
#endif // USE_ZLIB
			}
			if (cl_type == ClfType::Plain) { // ok, it is not compressed
				rewind(cl_plain);
			}
		}
	}

	if (cl_type == ClfType::Invalid) {
		//ErrorPrint("%s in ", buf);
		return -1;
	}
	return 0;
}

int CFile::PImpl::close()
{
	int ret = EOF;
	ClfType tp = cl_type;

	if (tp != ClfType::Invalid) {
		if (tp == ClfType::Plain) {
			ret = fclose(cl_plain);
		}
#ifdef USE_ZLIB
		if (tp == ClfType::Gzip) {
			ret = gzclose(cl_gz);
		}
#endif // USE_ZLIB
#ifdef USE_BZ2LIB
		if (tp == ClfType::Bzip2) {
			BZ2_bzclose(cl_bz);
			ret = 0;
		}
#endif // USE_BZ2LIB
	} else {
		errno = EBADF;
	}
	cl_type = ClfType::Invalid;
	return ret;
}

int CFile::PImpl::read(void *buf, size_t len)
{
	int ret = 0;

	if (cl_type != ClfType::Invalid) {
		if (cl_type == ClfType::Plain) {
			ret = fread(buf, 1, len, cl_plain);
		}
#ifdef USE_ZLIB
		if (cl_type == ClfType::Gzip) {
			ret = gzread(cl_gz, buf, len);
		}
#endif // USE_ZLIB
#ifdef USE_BZ2LIB
		if (cl_type == ClfType::Bzip2) {
			ret = BZ2_bzread(cl_bz, buf, len);
		}
#endif // USE_BZ2LIB
	} else {
		errno = EBADF;
	}
	return ret;
}

void CFile::PImpl::flush()
{
	if (cl_type != ClfType::Invalid) {
		if (cl_type == ClfType::Plain) {
			fflush(cl_plain);
		}
#ifdef USE_ZLIB
		if (cl_type == ClfType::Gzip) {
			gzflush(cl_gz, Z_SYNC_FLUSH);
		}
#endif // USE_ZLIB
#ifdef USE_BZ2LIB
		if (cl_type == ClfType::Bzip2) {
			BZ2_bzflush(cl_bz);
		}
#endif // USE_BZ2LIB
	} else {
		errno = EBADF;
	}
}

int CFile::PImpl::write(const void *buf, size_t size)
{
	ClfType tp = cl_type;
	int ret = -1;

	if (tp != ClfType::Invalid) {
		if (tp == ClfType::Plain) {
			ret = fwrite(buf, size, 1, cl_plain);
		}
#ifdef USE_ZLIB
		if (tp == ClfType::Gzip) {
			ret = gzwrite(cl_gz, buf, size);
		}
#endif // USE_ZLIB
#ifdef USE_BZ2LIB
		if (tp == ClfType::Bzip2) {
			ret = BZ2_bzwrite(cl_bz, const_cast<void *>(buf), size);
		}
#endif // USE_BZ2LIB
	} else {
		errno = EBADF;
	}
	return ret;
}

int CFile::PImpl::seek(long offset, int whence)
{
	int ret = -1;
	ClfType tp = cl_type;

	if (tp != ClfType::Invalid) {
		if (tp == ClfType::Plain) {
			ret = fseek(cl_plain, offset, whence);
		}
#ifdef USE_ZLIB
		if (tp == ClfType::Gzip) {
			ret = gzseek(cl_gz, offset, whence);
		}
#endif // USE_ZLIB
#ifdef USE_BZ2LIB
		if (tp == ClfType::Bzip2) {
			bzseek(cl_bz, offset, whence);
			ret = 0;
		}
#endif // USE_BZ2LIB
	} else {
		errno = EBADF;
	}
	return ret;
}

long CFile::PImpl::tell()
{
	int ret = -1;
	ClfType tp = cl_type;

	if (tp != ClfType::Invalid) {
		if (tp == ClfType::Plain) {
			ret = ftell(cl_plain);
		}
#ifdef USE_ZLIB
		if (tp == ClfType::Gzip) {
			ret = gztell(cl_gz);
		}
#endif // USE_ZLIB
#ifdef USE_BZ2LIB
		if (tp == ClfType::Bzip2) {
			// FIXME: need to implement this
			ret = -1;
		}
#endif // USE_BZ2LIB
	} else {
		errno = EBADF;
	}
	return ret;
}


/**
**  Find a file with its correct extension ("", ".gz" or ".bz2")
**
**  @param fullpath  the file path. Upon success, the path
**                   is replaced by the full filename with the correct extension.
**
**  @return true if the file has been found.
*/
static bool FindFileWithExtension(fs::path &fullpath)
{
	if (fs::exists(fullpath)) {
		return true;
	}
#if defined(USE_ZLIB) || defined(USE_BZ2LIB)
	auto directory = fullpath.parent_path();
	auto filename = fullpath.filename().string();
#endif
#ifdef USE_ZLIB // gzip or bzip2 in global shared directory
	if (fs::exists(directory / (filename + ".gz"))) {
		fullpath = directory / (filename + ".gz");
		return true;
	}
#endif
#ifdef USE_BZ2LIB
	if (fs::exists(directory / (filename + ".bz2"))) {
		fullpath = directory / (filename + ".bz2");
		return true;
	}
#endif
	return false;
}

/**
**  Generate a filename into library.
**
**  Try current directory, user home directory, global directory.
**  This supports .gz, .bz2 and .zip.
**
**  @param file        Filename to open.
**  return generated filename.
*/
// WargusHD: the classic WC2 campaign maps are the player's own original .PUD files. When a campaign
// presentation map (campaigns/<race>[-exp]/levelNN<r>.smp) is requested but not present as a
// Stratagus map, convert the matching source .PUD -- from the user-selected Warcraft II folder
// (WC2SourcePath) -- to a Stratagus .smp/.sms in a writable cache and return that. This makes the
// engine read the original campaign maps directly, with no external extractor: the converter is
// built into the app, so a single APK reads the original + Remastered files on its own.
// Resolve a campaigns/<race>[-exp]/levelNN.smp request to its source .PUD in the user's WC2 folder, or {}.
static fs::path ResolveCampaignPud(const std::string_view fileview)
{
	if (WC2SourcePath.empty()) {
		return {};
	}
	std::string f(fileview);
	for (char &c : f) { if (c == '\\') { c = '/'; } }
	const auto pos = f.find("campaigns/");
	if (pos == std::string::npos) {
		return {};
	}
	std::string rest = f.substr(pos + 10);
	const auto slash = rest.find('/');
	if (slash == std::string::npos) { return {}; }
	const std::string dir = rest.substr(0, slash);
	const std::string base = rest.substr(slash + 1);
	auto dot = base.find(".smp");
	if (dot == std::string::npos) { dot = base.find(".sms"); }
	if (dot == std::string::npos) { return {}; }
	const std::string stem = base.substr(0, dot);
	if (stem.compare(0, 5, "level") != 0) { return {}; }
	// Only the PLAIN map files (levelNN.smp / .sms) are PUD-backed. The campaign briefing/trigger wrappers
	// (levelNN_c.sms, levelNN_c2.sms) are real script files -> reject them so they load normally and their
	// own Load() of the plain .sms lands back on the native path.
	if (stem.find('_') != std::string::npos) { return {}; }
	size_t digitStart = 5;
	if (digitStart < stem.size() && (stem[digitStart] == 'x' || stem[digitStart] == 'X')) {
		++digitStart;
	}
	std::string num;
	for (size_t i = digitStart; i < stem.size() && std::isdigit((unsigned char) stem[i]); ++i) { num += stem[i]; }
	if (num.empty()) { return {}; }
	if (num.size() == 1) { num = "0" + num; }
	const bool exp = dir.find("-exp") != std::string::npos;
	const bool orc = dir.compare(0, 3, "orc") == 0;
	std::string sub, prefix;
	if (!orc && !exp)      { sub = "Human";  prefix = "HUMAN"; }
	else if (orc && !exp)  { sub = "Orc";    prefix = "ORC"; }
	else if (!orc && exp)  { sub = "XHuman"; prefix = "2XHUM"; }
	else                   { sub = "XOrc";   prefix = "2XORC"; }
	std::error_code ec;
	fs::path pud = fs::path(WC2SourcePath) / "Data" / "Campaign" / sub / (prefix + num + ".PUD");
	if (!fs::exists(pud, ec)) {
		return {};
	}
	return pud;
}

// ---- Native in-memory PUD map loader (no .smp/.sms files, no wc2mapcache) ----
// Reads the original .PUD and runs the SAME map-construction Lua the converter emitted, directly via the
// engine. The classic PudToStratagus path is retained as a fallback until this is device-verified.
static fs::path g_nativePudPath;
static std::string g_nativePudBytes;
static wc2pud::PudData g_nativePudData;
static bool g_nativePudReady = false;

// Read + parse a .PUD once; cache bytes + parsed data for the two load phases + the UDTA/UGRD reader.
static bool EnsureNativePud(const fs::path &pudPath)
{
	if (g_nativePudReady && g_nativePudPath == pudPath) {
		return true;
	}
	g_nativePudReady = false;
	FILE *fp = fopen(pudPath.string().c_str(), "rb");
	if (!fp) {
		return false;
	}
	fseek(fp, 0, SEEK_END);
	const long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz <= 0) { fclose(fp); return false; }
	std::string bytes;
	bytes.resize((size_t) sz);
	const size_t rd = fread(&bytes[0], 1, (size_t) sz, fp);
	fclose(fp);
	if (rd != (size_t) sz) {
		return false;
	}
	wc2pud::PudData pd;
	if (!pd.Parse((const unsigned char *) bytes.data(), bytes.size())) {
		return false;
	}
	g_nativePudPath = pudPath;
	g_nativePudBytes = std::move(bytes);
	g_nativePudData = std::move(pd);
	g_nativePudReady = true;
	return true;
}

// Resolve any map request to its source .PUD (direct .pud path or campaign levelNN.smp), or {}.
fs::path ResolveMapPud(const std::string &request)
{
	fs::path p(request);
	const std::string ext = p.extension().string();
	if (ext == ".pud" || ext == ".PUD" || ext == ".Pud") {
		const fs::path r = fs::path(LibraryFileName(request));
		std::error_code ec;
		if (!r.empty() && fs::exists(r, ec)) {
			return r;
		}
		return {};
	}
	return ResolveCampaignPud(request);
}

// Load a PUD map natively. phase 0 = presentation (Map.Info); 1 = setup (players/tiles/units).
// Returns false on any failure so the caller can fall back to the converter.
bool LoadPudMapNative(const fs::path &pudPath, int phase)
{
	if (!EnsureNativePud(pudPath)) {
		return false;
	}
	std::string lua;
	if (phase == 0) {
		g_nativePudData.BuildPresentation(lua);
	} else {
		g_nativePudData.BuildSetup(lua);
	}
	return CclCommand(lua, false) == 0;
}

// Raw bytes of the currently-loaded native PUD (for the per-map UDTA/UGRD reader), or nullptr.
const std::string *CurrentNativePudBytes()
{
	return g_nativePudReady ? &g_nativePudBytes : nullptr;
}

static fs::path LibraryFileNameImpl(const std::string_view file)
{
	// Absolute path or in current directory.
	fs::path candidate = file;
	if (candidate.is_absolute()) {
		return candidate;
	}
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}

	// Try in map directory
	if (*CurrentMapPath) {
		if (*CurrentMapPath == '.' || *CurrentMapPath == '/') {
			candidate = fs::path(CurrentMapPath) / file;
		} else {
			candidate = fs::path(StratagusLibPath) / CurrentMapPath / file;
		}
		if (FindFileWithExtension(candidate)) {
			return candidate;
		}
	}

	// In user home directory
	if (!GameName.empty()) {
		candidate = Parameters::Instance.GetUserDirectory() / GameName / file;
		if (FindFileWithExtension(candidate)) {
			return candidate;
		}
	}

	// In global shared directory
	candidate = fs::path(StratagusLibPath) / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}

	// WargusHD: the user-selected Warcraft II: Remastered source folder (files/wc2_source.txt),
	// so Remastered assets (e.g. Data/Backgrounds/*.png, Data/skins/*, Data/Font/*) resolve from
	// where the player installed the game, chosen at the first-run gate (SetupActivity).
	if (!WC2SourcePath.empty()) {
		candidate = fs::path(WC2SourcePath) / file;
		if (FindFileWithExtension(candidate)) {
			return candidate;
		}
	}

	// Support for graphics in default graphics dir.
	// They could be anywhere now, but check if they haven't
	// got full paths.
	candidate = fs::path("graphics") / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}
	candidate = fs::path(StratagusLibPath) / "graphics" / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}

	// Support for sounds in default sounds dir.
	// They could be anywhere now, but check if they haven't
	// got full paths.
	candidate = fs::path("sounds") / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}
	candidate = fs::path(StratagusLibPath) / "sounds" / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}

	// Support for scripts in default scripts dir.
	candidate = fs::path("scripts") / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}
	candidate = fs::path(StratagusLibPath) / "scripts" / file;
	if (FindFileWithExtension(candidate)) {
		return candidate;
	}

	DebugPrint("File '%s' not found\n", file.data());
	return file;
}

extern std::string LibraryFileName(const std::string &file)
{
	static std::unordered_map<std::string, std::string> FileNameMap;
	auto result = FileNameMap.find(file);
	if (result == std::end(FileNameMap)) {
		fs::path path = LibraryFileNameImpl(file);
		std::string r(path.string());
		FileNameMap[file] = r;
		return r;
	} else {
		return result->second;
	}
}

bool CanAccessFile(const char *filename)
{
	if (filename && filename[0] != '\0') {
		const auto path = LibraryFileNameImpl(filename);
		return fs::exists(path);
	}
	return false;
}

/**
**  Generate a list of files within a specified directory
**
**  @param dirname  Directory to read.
**
**  @return list of files/directories.
*/
std::vector<FileList> ReadDataDirectory(const fs::path& directory)
{
	if (!fs::exists(directory) || !fs::is_directory(directory)) {
		return {};
	}
	std::vector<FileList> files;

	for (auto it = fs::directory_iterator{directory};
	     it != fs::directory_iterator{};
	     ++it) {
		if (fs::is_directory(it->path())) {
			files.emplace_back();
			files.back().name = it->path().filename();
		} else if (fs::is_regular_file(it->path())) {
			files.emplace_back();
			files.back().name = it->path().filename();
			files.back().type = 1;
		}
	}
	ranges::sort(files);
	return files;
}

class RawFileWriter : public FileWriter
{
	FILE *file;

public:
	explicit RawFileWriter(const fs::path &filename)
	{
		file = fopen(filename.string().c_str(), "wb");
		if (!file) {
			ErrorPrint("Can't open file '%s' for writing\n", filename.u8string().c_str());
			throw FileException();
		}
	}

	virtual ~RawFileWriter()
	{
		if (file) { fclose(file); }
	}

	int write(std::string_view data) override { return fwrite(data.data(), data.size(), 1, file); }
};

class GzFileWriter : public FileWriter
{
	gzFile file;

public:
	explicit GzFileWriter(const fs::path &filename)
	{
		file = gzopen(filename.string().c_str(), "wb9");
		if (!file) {
			ErrorPrint("Can't open file '%s' for writing\n", filename.u8string().c_str());
			throw FileException();
		}
	}

	virtual ~GzFileWriter()
	{
		if (file) { gzclose(file); }
	}

	int write(std::string_view data) override
	{
		return gzwrite(file, data.data(), data.size());
	}
};

/**
**  Create FileWriter
*/
std::unique_ptr<FileWriter> CreateFileWriter(const fs::path &filename)
{
	if (filename.extension() == ".gz") {
		return std::make_unique<GzFileWriter>(filename);
	} else {
		return std::make_unique<RawFileWriter>(filename);
	}
}

/**
 * Quote arguments for usage in calls to system(), popen() and similar.
 * Really only needed on Windows, where all these calls just concatenate
 * all arguments with a space and pass the full string to the next process.
 */
template <typename CHAR>
std::vector<std::basic_string<CHAR>> QuoteArgumentsImpl(const std::vector<std::basic_string<CHAR>>& args, const CHAR* spaces, CHAR quote, CHAR escape)
{
	std::vector<std::basic_string<CHAR>> outArgs;
	for (const auto& arg : args) {
#ifdef WIN32
		if (!arg.empty() && arg.find_first_of(spaces) == std::basic_string<CHAR>::npos) {
			outArgs.push_back(arg);
		} else {
			// Windows always needs argument quoting around arguments with spaces
			std::basic_string<CHAR> ss(1, quote);
			for (auto ch = arg.begin(); ; ch++) {
				int backslashes = 0;
				while (ch != arg.end() && *ch == escape) {
					ch++;
					backslashes++;
				}
				if (ch == arg.end()) {
					ss.append(backslashes * 2, escape);
					break;
				} else if (*ch == quote) {
					ss.append(backslashes * 2 + 1, escape);
					ss.push_back(*ch);
				} else {
					ss.append(backslashes, escape);
					ss.push_back(*ch);
				}
			}
			ss.push_back(quote);
			outArgs.push_back(ss);
		}
#else
		outArgs.push_back(arg);
#endif
	}
	return outArgs;
}

std::vector<std::string> QuoteArguments(const std::vector<std::string>& args)
{
	return QuoteArgumentsImpl(args, " \t\n\v\"", '"', '\\');
}

std::vector<std::wstring> QuoteArguments(const std::vector<std::wstring>& args)
{
	return QuoteArgumentsImpl(args, L" \t\n\v\"", L'"', L'\\');
}

//@}
