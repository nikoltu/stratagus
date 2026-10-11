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
/**@name test_net_mesh.cpp - The test file for net_mesh.cpp and net_mesh_wire.cpp. */
//
//      (c) Copyright 2026 by The Stratagus Project
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

#include <doctest.h>

#include "stratagus.h"
#include "net_mesh.h"
#include "net_mesh_wire.h"
#include "net_message.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <vector>

// The helpers live inside the namespace of the model, so that its short names (Kind, Reason,
// Event) cannot meet an engine name.
namespace mesh {
namespace wire_test {

static const Kind AllKinds[] = {Kind::Cmd, Kind::Loading, Kind::Loaded, Kind::Need, Kind::Status, Kind::Commit, Kind::Cancel, Kind::Pause};

static bool Same(const Msg &a, const Msg &b)
{
	if (a.kind != b.kind || a.player != b.player || a.flags != b.flags || a.round != b.round
		|| a.cycle != b.cycle || a.data != b.data || a.mask != b.mask || a.removed != b.removed
		|| a.want != b.want) {
		return false;
	}
	for (int p = 0; p < kMaxPlayers; ++p) {
		if (a.rounds[p] != b.rounds[p] || a.have[p] != b.have[p]) return false;
	}
	return true;
}

// A message of that kind with every field it carries at its highest value (8 players).
static Msg Highest(Kind kind)
{
	Msg m;
	m.kind = kind;
	switch (kind) {
	case Kind::Cmd:
		m.player = kMaxPlayers - 1;
		m.flags = 1;
		m.cycle = kWireMaxCycle;
		m.data = 0xFFFFFFFFu;
		break;
	case Kind::Loading:
		break;
	case Kind::Loaded:
		m.mask = 0xFFFF;
		break;
	case Kind::Need:
		m.flags = 1;
		m.cycle = kWireMaxCycle;
		m.mask = 0xFFFF;
		break;
	case Kind::Status:
		m.player = kMaxPlayers - 1;
		m.data = 0xFFFFFFFFu;
		m.mask = m.removed = m.want = 0xFFFF;
		for (int p = 0; p < kMaxPlayers; ++p) {
			m.rounds[p] = 0xFFFF;
			m.have[p] = kWireMaxCycle;
		}
		break;
	case Kind::Commit:
		m.player = kMaxPlayers - 1;
		m.flags = uint8_t(Reason::Desync);
		m.round = 0xFFFF;
		m.cycle = kWireMaxCycle;
		m.mask = 0xFFFF;
		break;
	case Kind::Cancel:
		m.player = kMaxPlayers - 1;
		m.round = 0xFFFF;
		break;
	case Kind::Pause:
		m.player = kMaxPlayers - 1;
		m.flags = 1;
		m.cycle = kWireMaxCycle;
		break;
	}
	return m;
}

// A valid message of a random kind with random values in the fields the kind carries.
template <typename Rng>
static Msg Random(Rng &rng)
{
	Msg m;
	m.kind = AllKinds[rng() % 8];
	const auto cycle = [&]() { return int32_t(rng() % (uint32_t(kWireMaxCycle) + 1)); };
	switch (m.kind) {
	case Kind::Cmd:
		m.player = uint8_t(rng() % kMaxPlayers);
		m.flags = uint8_t(rng() % 2);
		m.cycle = cycle();
		m.data = uint32_t(rng());
		break;
	case Kind::Loading:
		break;
	case Kind::Loaded:
		m.mask = uint16_t(rng());
		break;
	case Kind::Need:
		m.flags = uint8_t(rng() % 2);
		m.cycle = cycle();
		m.mask = uint16_t(rng());
		break;
	case Kind::Status:
		m.player = uint8_t(rng() % kMaxPlayers);
		m.data = uint32_t(rng());
		m.mask = uint16_t(rng());
		m.removed = uint16_t(rng());
		m.want = uint16_t(rng());
		for (int p = 0; p < kMaxPlayers; ++p) {
			m.rounds[p] = uint16_t(rng());
			m.have[p] = cycle() - 1;
		}
		break;
	case Kind::Commit:
		m.player = uint8_t(rng() % kMaxPlayers);
		m.flags = uint8_t(rng() % (unsigned(Reason::Desync) + 1));
		m.round = uint16_t(rng());
		m.cycle = cycle();
		m.mask = uint16_t(rng());
		break;
	case Kind::Cancel:
		m.player = uint8_t(rng() % kMaxPlayers);
		m.round = uint16_t(rng());
		break;
	case Kind::Pause:
		m.player = uint8_t(rng() % kMaxPlayers);
		m.flags = uint8_t(rng() % 2);
		m.cycle = cycle();
		break;
	}
	return m;
}

static bool Parses(const unsigned char *buf, size_t len)
{
	Msg m;
	int from = -1;
	return Deserialize(buf, len, &m, &from);
}

// Is a changed copy of a valid datagram refused? `at` = the byte, `value` = what is put there.
static bool RefusedWith(Kind kind, size_t at, unsigned char value)
{
	unsigned char buf[kWireMaxSize];
	Msg m;
	m.kind = kind;
	const size_t len = Serialize(m, 0, buf, sizeof buf);
	if (len == 0 || at >= len || !Parses(buf, len)) return false;
	buf[at] = value;
	return !Parses(buf, len);
}

TEST_CASE("mesh wire: the stated length of every kind, all far below one datagram")
{
	CHECK(kWireCmdSize == 14);
	CHECK(kWireLoadingSize == 4);
	CHECK(kWireLoadedSize == 6);
	CHECK(kWireNeedSize == 11);
	CHECK(kWireStatusSize == 63);
	CHECK(kWireCommitSize == 14);
	CHECK(kWireCancelSize == 7);
	CHECK(kWirePauseSize == 10);
	CHECK(kWireMaxSize == 63);
	CHECK(kWireMaxSize < 1200);
	for (const Kind kind : AllKinds) {
		unsigned char buf[kWireMaxSize];
		CHECK(Serialize(Highest(kind), kMaxPlayers - 1, buf, sizeof buf) == WireSize(kind));
		CHECK(WireSize(kind) <= kWireMaxSize);
	}
	CHECK(WireSize(Kind(7)) == 0);
	CHECK(WireSize(Kind(8)) == 0);
	CHECK(WireSize(Kind(9)) == 0);
	CHECK(WireSize(Kind(11)) == 0);
	CHECK(WireSize(Kind(255)) == 0);
	// One answer to a Need is at most lag + 1 command datagrams per asked player.
	CHECK((kLag + 1) * kWireCmdSize == 154);
}

TEST_CASE("mesh wire: the bytes of a commit")
{
	Msg m;
	m.kind = Kind::Commit;
	m.player = 5;
	m.flags = uint8_t(Reason::Left);
	m.round = 0x1234;
	m.cycle = 0x01020304;
	m.mask = 0xA0B1;
	unsigned char buf[kWireMaxSize];
	REQUIRE(Serialize(m, 3, buf, sizeof buf) == 14);
	const unsigned char expected[14] = {
		MessageMesh, kWireVersion, uint8_t(Kind::Commit), 3,
		5, uint8_t(Reason::Left), 0x12, 0x34, 0x01, 0x02, 0x03, 0x04, 0xA0, 0xB1};
	CHECK(std::memcmp(buf, expected, sizeof expected) == 0);
}

TEST_CASE("mesh wire: the bytes of a pause")
{
	Msg m;
	m.kind = Kind::Pause;
	m.player = 6;
	m.flags = 1;
	m.cycle = 0x01020304;
	unsigned char buf[kWireMaxSize];
	REQUIRE(Serialize(m, 2, buf, sizeof buf) == 10);
	const unsigned char expected[10] = {MessageMesh, kWireVersion, 10, 2, 6, 1, 0x01, 0x02, 0x03, 0x04};
	CHECK(std::memcmp(buf, expected, sizeof expected) == 0);
	// the kind byte lies above the probe (7), its answer (8) and the relay (9)
	CHECK(uint8_t(Kind::Pause) > kWireRelay);
	bool ack = false;
	int from = -1;
	CHECK_FALSE(DeserializeProbe(buf, 10, &ack, &from));
	// flags above 1, a player that is none
	CHECK(RefusedWith(Kind::Pause, 5, 2));
	CHECK(RefusedWith(Kind::Pause, 4, kMaxPlayers));
}

TEST_CASE("mesh wire: the type byte is none of the consecutive message types")
{
	CHECK(MessageMesh == 0x7F);
	CHECK(MessageCommandSpellCast == 33);
	CHECK(MessageExtendedCommand == 32);
	CHECK(MessageMesh > MessageCommandSpellCast);
	CHECK(MessageMesh != MessageInit_FromClient);
	CHECK(MessageMesh != MessageInit_FromServer);
}

TEST_CASE("mesh wire: every kind comes back as it was sent")
{
	std::mt19937 rng(4);
	for (const Kind kind : AllKinds) {
		CAPTURE(int(kind));
		// lowest values, highest values
		for (int high = 0; high < 2; ++high) {
			Msg m = Highest(kind);
			if (!high) {
				m = Msg();
				m.kind = kind;
				if (kind == Kind::Status) {
					for (int p = 0; p < kMaxPlayers; ++p) m.have[p] = -1;
				}
			}
			const int from = high ? kMaxPlayers - 1 : 0;
			unsigned char buf[kWireMaxSize];
			const size_t len = Serialize(m, from, buf, sizeof buf);
			REQUIRE(len == WireSize(kind));
			Msg back;
			int backFrom = -1;
			REQUIRE(Deserialize(buf, len, &back, &backFrom));
			CHECK(Same(m, back));
			CHECK(backFrom == from);
		}
	}
	// every player as sender and as subject, random values
	int same = 0;
	for (int i = 0; i < 20000; ++i) {
		const Msg m = Random(rng);
		const int from = int(rng() % kMaxPlayers);
		unsigned char buf[kWireMaxSize];
		const size_t len = Serialize(m, from, buf, sizeof buf);
		Msg back;
		int backFrom = -1;
		if (len == WireSize(m.kind) && Deserialize(buf, len, &back, &backFrom) && Same(m, back) && backFrom == from) {
			++same;
		}
	}
	CHECK(same == 20000);
}

TEST_CASE("mesh wire: only the fields of the kind travel")
{
	for (const Kind kind : AllKinds) {
		CAPTURE(int(kind));
		Msg all = Highest(Kind::Status);
		all.kind = kind;
		all.flags = 1;
		all.round = 0xFFFF;
		all.cycle = kWireMaxCycle;
		unsigned char buf[kWireMaxSize];
		const size_t len = Serialize(all, 1, buf, sizeof buf);
		REQUIRE(len == WireSize(kind));
		Msg back;
		int from = -1;
		REQUIRE(Deserialize(buf, len, &back, &from));
		// What came back, sent again, gives the same bytes: nothing else was on the wire.
		unsigned char again[kWireMaxSize];
		REQUIRE(Serialize(back, from, again, sizeof again) == len);
		CHECK(std::memcmp(buf, again, len) == 0);
		if (kind == Kind::Loading) {
			Msg empty;
			empty.kind = Kind::Loading;
			CHECK(Same(back, empty));
		}
		if (kind != Kind::Status) {
			CHECK(back.removed == 0);
			CHECK(back.want == 0);
			CHECK(back.rounds[kMaxPlayers - 1] == 0);
			CHECK(back.have[kMaxPlayers - 1] == 0);
		}
	}
}

TEST_CASE("mesh wire: every shorter and every longer datagram is refused")
{
	for (const Kind kind : AllKinds) {
		CAPTURE(int(kind));
		unsigned char whole[kWireMaxSize];
		const size_t len = Serialize(Highest(kind), kMaxPlayers - 1, whole, sizeof whole);
		REQUIRE(len == WireSize(kind));
		for (size_t cut = 0; cut < len; ++cut) {
			CAPTURE(cut);
			// A buffer of exactly `cut` bytes: a read beyond it is a read outside the allocation.
			std::unique_ptr<unsigned char[]> part(new unsigned char[cut ? cut : 1]);
			std::memcpy(part.get(), whole, cut);
			Msg m = Highest(Kind::Status);
			int from = 77;
			CHECK_FALSE(Deserialize(part.get(), cut, &m, &from));
			// a refused datagram leaves the results untouched
			CHECK(Same(m, Highest(Kind::Status)));
			CHECK(from == 77);
		}
		// longer: by one byte, up to the longest kind, beyond it, a full receive buffer
		for (const size_t longer : {len + 1, kWireMaxSize, kWireMaxSize + 1, size_t(1024), size_t(1500), size_t(65507)}) {
			if (longer == len) continue;
			CAPTURE(longer);
			std::vector<unsigned char> big(longer, 0);
			std::memcpy(big.data(), whole, len);
			CHECK_FALSE(Parses(big.data(), longer));
		}
		// the writer: too little room, no room, no buffer
		unsigned char small[kWireMaxSize];
		std::memset(small, 0xAA, sizeof small);
		CHECK(Serialize(Highest(kind), 0, small, len - 1) == 0);
		CHECK(Serialize(Highest(kind), 0, small, 0) == 0);
		CHECK(Serialize(Highest(kind), 0, nullptr, kWireMaxSize) == 0);
		CHECK(size_t(std::count(small, small + sizeof small, 0xAA)) == sizeof small);
	}
	CHECK_FALSE(Parses(nullptr, 0));
	CHECK_FALSE(Parses(nullptr, kWireCmdSize));
	unsigned char buf[kWireMaxSize] = {};
	int from = 0;
	Msg m;
	CHECK_FALSE(Deserialize(buf, sizeof buf, nullptr, &from));
	CHECK_FALSE(Deserialize(buf, sizeof buf, &m, nullptr));
}

TEST_CASE("mesh wire: another version, type or kind is refused")
{
	for (const Kind kind : AllKinds) {
		CAPTURE(int(kind));
		for (int v = 0; v < 256; ++v) {
			if (v != kWireVersion) CHECK(RefusedWith(kind, 1, uint8_t(v)));
			if (v != MessageMesh) CHECK(RefusedWith(kind, 0, uint8_t(v)));
		}
		// a kind byte that is no kind, or a kind of another length
		for (int k = 0; k < 256; ++k) {
			if (k > int(Kind::Pause) || WireSize(Kind(k)) != WireSize(kind)) {
				CHECK(RefusedWith(kind, 2, uint8_t(k)));
			}
		}
	}
}

TEST_CASE("mesh wire: a field outside its range is refused, on both sides")
{
	unsigned char buf[kWireMaxSize];
	// sender
	for (const Kind kind : AllKinds) {
		CAPTURE(int(kind));
		for (int v = kMaxPlayers; v < 256; ++v) CHECK(RefusedWith(kind, 3, uint8_t(v)));
		Msg m;
		m.kind = kind;
		CHECK(Serialize(m, -1, buf, sizeof buf) == 0);
		CHECK(Serialize(m, kMaxPlayers, buf, sizeof buf) == 0);
	}
	// player
	for (const Kind kind : {Kind::Cmd, Kind::Status, Kind::Commit, Kind::Cancel}) {
		CAPTURE(int(kind));
		for (int v = kMaxPlayers; v < 256; ++v) CHECK(RefusedWith(kind, 4, uint8_t(v)));
		Msg m;
		m.kind = kind;
		m.player = kMaxPlayers;
		CHECK(Serialize(m, 0, buf, sizeof buf) == 0);
	}
	// flags: Cmd byte 5, Need byte 4, Commit byte 5 (a Reason)
	for (int v = 2; v < 256; ++v) {
		CHECK(RefusedWith(Kind::Cmd, 5, uint8_t(v)));
		CHECK(RefusedWith(Kind::Need, 4, uint8_t(v)));
	}
	for (int v = int(Reason::Desync) + 1; v < 256; ++v) CHECK(RefusedWith(Kind::Commit, 5, uint8_t(v)));
	// cycles: the high byte 0x80 makes the number negative, 0x7F 0xFF is above the bound
	CHECK(RefusedWith(Kind::Cmd, 6, 0x80));
	CHECK(RefusedWith(Kind::Need, 5, 0x80));
	CHECK(RefusedWith(Kind::Commit, 8, 0x80));
	{
		unsigned char top[kWireMaxSize];
		Msg m;
		m.kind = Kind::Cmd;
		m.cycle = kWireMaxCycle;
		REQUIRE(Serialize(m, 0, top, sizeof top) == kWireCmdSize);
		CHECK(top[6] == 0x7F);
		CHECK(top[7] == 0xFE);
		CHECK(Parses(top, kWireCmdSize));
		top[7] = 0xFF;
		CHECK_FALSE(Parses(top, kWireCmdSize));
	}
	for (const Kind kind : {Kind::Cmd, Kind::Need, Kind::Commit}) {
		CAPTURE(int(kind));
		Msg m;
		m.kind = kind;
		m.cycle = -1;
		CHECK(Serialize(m, 0, buf, sizeof buf) == 0);
		m.cycle = kWireMaxCycle + 1;
		CHECK(Serialize(m, 0, buf, sizeof buf) == 0);
		m.cycle = kNever;
		CHECK(Serialize(m, 0, buf, sizeof buf) == 0);
		m.cycle = kWireMaxCycle;
		CHECK(Serialize(m, 0, buf, sizeof buf) == WireSize(kind));
	}
	// have: -1 is the lowest
	for (int p = 0; p < kMaxPlayers; ++p) {
		CAPTURE(p);
		Msg m;
		m.kind = Kind::Status;
		m.have[p] = -1;
		const size_t len = Serialize(m, 0, buf, sizeof buf);
		REQUIRE(len == kWireStatusSize);
		CHECK(Parses(buf, len));
		buf[kWireStatusSize - (kMaxPlayers - p) * 4 + 3] = 0xFE; // -2
		CHECK_FALSE(Parses(buf, len));
		m.have[p] = -2;
		CHECK(Serialize(m, 0, buf, sizeof buf) == 0);
		m.have[p] = kWireMaxCycle + 1;
		CHECK(Serialize(m, 0, buf, sizeof buf) == 0);
	}
	Msg bad;
	bad.kind = Kind(7);
	CHECK(Serialize(bad, 0, buf, sizeof buf) == 0);
}

// 200 000 buffers, half random, half valid datagrams with damage. Every one is parsed twice, once
// with 0x00 and once with 0xFF around it: the answer may depend on nothing outside the datagram.
// What is accepted must have the length of its kind and give the same bytes when sent again, and
// is handed to a model, which must take it without a fault.
TEST_CASE("mesh wire: fuzz of the parser")
{
	std::mt19937 rng(20261010);
	const size_t pad = 16;
	std::vector<unsigned char> lo, hi, data;
	int accepted = 0, refused = 0, differs = 0, notCanonical = 0;
	std::unique_ptr<Mesh> dev;
	int64_t now = 0;
	for (int i = 0; i < 200000; ++i) {
		data.clear();
		if (i % 2 == 0) {
			size_t len = rng() % (kWireMaxSize + 9);
			if (rng() % 64 == 0) len = rng() % 1501;
			data.resize(len);
			for (auto &b : data) b = uint8_t(rng());
			if (len >= kWireHeaderSize && rng() % 4 != 0) {
				data[0] = MessageMesh;
				data[1] = kWireVersion;
				if (rng() % 2) {
					data[2] = uint8_t(rng() % 11);
					data[3] = uint8_t(rng() % (kMaxPlayers + 1));
					if (rng() % 2) {
						data.resize(WireSize(Kind(data[2])));
						for (size_t k = kWireHeaderSize; k < data.size(); ++k) data[k] = uint8_t(rng() % 4 ? rng() % 8 : rng());
					}
				}
			}
		} else {
			unsigned char buf[kWireMaxSize];
			const size_t len = Serialize(Random(rng), int(rng() % kMaxPlayers), buf, sizeof buf);
			data.assign(buf, buf + len);
			for (int k = int(rng() % 4); k > 0; --k) {
				switch (rng() % 5) {
				case 0: if (!data.empty()) data[rng() % data.size()] ^= uint8_t(1u << (rng() % 8)); break;
				case 1: if (!data.empty()) data[rng() % data.size()] = uint8_t(rng()); break;
				case 2: data.resize(rng() % (data.size() + 1)); break;
				case 3: data.resize(data.size() + 1 + rng() % 8, uint8_t(rng())); break;
				case 4: if (data.size() > kWireHeaderSize) data[kWireHeaderSize + rng() % (data.size() - kWireHeaderSize)] = uint8_t(rng() % 9); break;
				}
			}
		}
		const size_t len = data.size();
		lo.assign(len + 2 * pad, 0x00);
		hi.assign(len + 2 * pad, 0xFF);
		if (len) {
			std::memcpy(lo.data() + pad, data.data(), len);
			std::memcpy(hi.data() + pad, data.data(), len);
		}
		Msg a = Highest(Kind::Status), b = Highest(Kind::Status);
		int fromA = 77, fromB = 77;
		const bool okA = Deserialize(lo.data() + pad, len, &a, &fromA);
		const bool okB = Deserialize(hi.data() + pad, len, &b, &fromB);
		if (okA != okB || !Same(a, b) || fromA != fromB) ++differs;
		if (!okA) {
			++refused;
			if (!Same(a, Highest(Kind::Status)) || fromA != 77) ++differs;
			continue;
		}
		++accepted;
		unsigned char again[kWireMaxSize];
		if (len != WireSize(a.kind) || Serialize(a, fromA, again, sizeof again) != len || std::memcmp(again, data.data(), len) != 0) {
			++notCanonical;
			continue;
		}
		if (!dev || dev->IsOut()) {
			dev.reset(new Mesh(kMaxPlayers, int(rng() % kMaxPlayers), Params(), now));
			dev->OnLoaded(now);
		}
		now += 1 + rng() % 40;
		dev->OnMessage(fromA, a, now);
		if (rng() % 8 == 0) dev->Tick(now);
		if (dev->CanExecute(dev->Executed() + 1)) dev->OnExecuted(dev->Executed() + 1);
		dev->out.clear();
		dev->events.clear();
	}
	CHECK(accepted + refused == 200000);
	CHECK(accepted > 20000);
	CHECK(refused > 20000);
	CHECK(differs == 0);
	CHECK(notCanonical == 0);
	MESSAGE("fuzz: accepted " << accepted << ", refused " << refused);
}

// ------------------------------------------------------------------------------------------------
// The simulator of the model (lan-mesh-sim), cut down to deaths, silences, quits and slow loaders,
// with one difference: with `wire` every message is turned into bytes by the sender and parsed
// back by the receiver, so a device knows of its peers only what the bytes carry. The same run
// without `wire` passes the structs; both must end in the same history, cycle by cycle.

struct Fault {
	enum Type { Die, Silent, Quit, Pause, Resume } type; // Pause, Resume: device a presses the button
	int64_t t;
	int a;
	int64_t dur;
};

struct Config {
	int n = 3;
	uint64_t seed = 1000;
	double loss = 0.02, dup = 0.01;
	int dmin = 5, dmax = 40;
	int64_t endMs = 100000;
	std::vector<int64_t> loadAt;
	std::vector<Fault> faults;
};

struct Result {
	int n = 0;
	std::string why; // empty = the run kept its invariants
	int32_t executed[kMaxPlayers] = {}, execBefore[kMaxPlayers] = {};
	bool dead[kMaxPlayers] = {}, out[kMaxPlayers] = {};
	Reason outReason[kMaxPlayers] = {};
	int32_t removedAt[kMaxPlayers][kMaxPlayers];
	Reason reason[kMaxPlayers][kMaxPlayers];
	int64_t removedTime[kMaxPlayers][kMaxPlayers];
	int64_t startedAt[kMaxPlayers];
	std::vector<uint64_t> hash[kMaxPlayers];
	long long msgs = 0, bytes = 0;
	long kinds[11] = {};
	int pauses[kMaxPlayers] = {}, resumes[kMaxPlayers] = {};
	int32_t pauseCycle[kMaxPlayers] = {};
	int pausedBy[kMaxPlayers] = {}, resumedBy[kMaxPlayers] = {};
	bool paused[kMaxPlayers] = {};
	size_t longest = 0;
	int unsent = 0, unparsed = 0;
};

// The command that is a pause in this simulator; no other command has this value.
static const uint32_t PauseCommand = 0x50415553u;

static inline uint64_t Mix(uint64_t x)
{
	x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
	return x;
}

static Result Run(const Config &cfg, bool wire)
{
	const int n = cfg.n;
	Result res;
	res.n = n;
	std::mt19937_64 rng(cfg.seed);
	auto rnd = [&] { return (rng() >> 11) * (1.0 / 9007199254740992.0); };
	struct Pkt { int64_t at; uint64_t ord; int from, to; Msg m; unsigned char bytes[kWireMaxSize]; size_t len; };
	auto later = [](const Pkt &x, const Pkt &y) { return x.at != y.at ? x.at > y.at : x.ord > y.ord; };
	std::priority_queue<Pkt, std::vector<Pkt>, decltype(later)> net(later);
	uint64_t ord = 0;

	std::vector<std::unique_ptr<Mesh>> dev;
	bool loaded[kMaxPlayers] = {};
	bool wantPause[kMaxPlayers] = {};
	int64_t silentUntil[kMaxPlayers] = {};
	for (int i = 0; i < kMaxPlayers; ++i) {
		if (i < n) dev.emplace_back(new Mesh(n, i, Params(), 0));
		res.startedAt[i] = -1;
		for (int p = 0; p < kMaxPlayers; ++p) {
			res.removedAt[i][p] = kNever;
			res.removedTime[i][p] = -1;
			res.reason[i][p] = Reason::Lost;
		}
	}
	auto blocked = [&](int a, int b, int64_t now) {
		return res.dead[a] || res.dead[b] || now < silentUntil[a] || now < silentUntil[b];
	};
	std::vector<Fault> faults = cfg.faults;
	std::stable_sort(faults.begin(), faults.end(), [](const Fault &x, const Fault &y) { return x.t < y.t; });
	size_t nextFault = 0;

	for (int64_t now = 0; now <= cfg.endMs; now += 10) {
		while (nextFault < faults.size() && faults[nextFault].t <= now) {
			const Fault &f = faults[nextFault++];
			switch (f.type) {
			case Fault::Die: {
				// Dying mid-send: each packet of the device still in flight is lost with 50 %.
				std::vector<Pkt> keep;
				while (!net.empty()) {
					const Pkt p = net.top();
					net.pop();
					if (p.from != f.a || rnd() < 0.5) keep.push_back(p);
				}
				for (const Pkt &p : keep) net.push(p);
				res.dead[f.a] = true;
				break;
			}
			case Fault::Silent:
				silentUntil[f.a] = now + f.dur;
				break;
			case Fault::Quit:
				if (!res.dead[f.a]) dev[f.a]->Quit(now);
				break;
			case Fault::Pause:
				wantPause[f.a] = !res.dead[f.a] && !dev[f.a]->IsPaused();
				break;
			case Fault::Resume:
				if (!res.dead[f.a]) dev[f.a]->Resume(now);
				break;
			}
		}
		while (!net.empty() && net.top().at <= now) {
			const Pkt p = net.top();
			net.pop();
			if (blocked(p.from, p.to, now)) continue;
			++res.msgs;
			if (!wire) {
				dev[p.to]->OnMessage(p.from, p.m, now);
				continue;
			}
			Msg m;
			int from = -1;
			if (!Deserialize(p.bytes, p.len, &m, &from) || from != p.from) {
				++res.unparsed;
				continue;
			}
			res.bytes += p.len;
			dev[p.to]->OnMessage(p.from, m, now);
		}
		if (now == cfg.endMs - 5000) {
			for (int i = 0; i < n; ++i) res.execBefore[i] = dev[i]->Executed();
		}
		for (int i = 0; i < n; ++i) {
			if (res.dead[i]) continue;
			Mesh &d = *dev[i];
			const int64_t la = i < (int)cfg.loadAt.size() ? cfg.loadAt[i] : 0;
			if (!loaded[i] && now >= la) {
				loaded[i] = true;
				d.OnLoaded(now);
			}
			d.Tick(now);
			if (res.startedAt[i] >= 0) {
				const int32_t target = int32_t((now - res.startedAt[i]) * 30 / 1000);
				for (int k = 0; k < 10 && d.Executed() < target && d.CanExecute(d.Executed() + 1); ++k) {
					const int32_t c = d.Executed() + 1;
					const uint16_t live = d.LiveMaskAt(c);
					uint64_t h = Mix(uint64_t(c) * 0x9e3779b97f4a7c15ULL + live);
					for (int p = 0; p < n; ++p) {
						if ((live >> p) & 1) h = Mix(h ^ (uint64_t(d.Command(p, c)) + (uint64_t(p) << 40)));
					}
					res.hash[i].push_back(h);
					uint32_t command = uint32_t(rng());
					if (command == PauseCommand) command = 0;
					if (wantPause[i]) {
						wantPause[i] = false;
						command = PauseCommand;
					}
					d.SetCommand(command);
					d.OnExecuted(c);
					for (int p = 0; p < n; ++p) {
						if (((live >> p) & 1) && d.Command(p, c) == PauseCommand) d.Pause(p, now);
					}
				}
			}
			for (const Event &e : d.events) {
				switch (e.type) {
				case Event::Started:
					res.startedAt[i] = now;
					break;
				case Event::PlayerRemoved:
					res.removedAt[i][e.player] = e.cycle;
					res.reason[i][e.player] = e.reason;
					res.removedTime[i][e.player] = now;
					break;
				case Event::SelfOut:
					res.out[i] = true;
					res.outReason[i] = e.reason;
					if (e.reason == Reason::Desync) res.why = "a device stopped as desynchronised";
					break;
				case Event::Waiting:
					break;
				case Event::Paused:
					++res.pauses[i];
					res.pauseCycle[i] = e.cycle;
					res.pausedBy[i] = e.player;
					break;
				case Event::Resumed:
					++res.resumes[i];
					res.resumedBy[i] = e.player;
					break;
				}
			}
			d.events.clear();
			for (const Out &o : d.out) {
				if (blocked(i, o.to, now) || rnd() < cfg.loss) continue;
				Pkt p{};
				p.from = i;
				p.to = o.to;
				if (wire) {
					p.len = Serialize(o.msg, i, p.bytes, sizeof p.bytes);
					if (p.len == 0) ++res.unsent;
					res.longest = std::max(res.longest, p.len);
					++res.kinds[int(o.msg.kind)];
				} else {
					p.m = o.msg;
				}
				p.at = now + cfg.dmin + int64_t(rnd() * (cfg.dmax - cfg.dmin + 1));
				p.ord = ord++;
				net.push(p);
				if (rnd() < cfg.dup) {
					p.at = now + cfg.dmin + int64_t(rnd() * (cfg.dmax - cfg.dmin + 1));
					p.ord = ord++;
					net.push(p);
				}
			}
			d.out.clear();
		}
	}
	for (int i = 0; i < n; ++i) {
		res.executed[i] = dev[i]->Executed();
		res.paused[i] = dev[i]->IsPaused();
		if (dev[i]->conflicts) res.why = "a device received two different commits for one player";
	}
	// The invariant of the simulator: two devices still in one game have the same history.
	auto gone = [&](int i) { return res.dead[i] || (res.out[i] && res.outReason[i] != Reason::Left); };
	for (int i = 0; i < n; ++i) {
		for (int j = i + 1; j < n; ++j) {
			if (gone(i) || gone(j)
				|| (res.removedAt[i][j] != kNever && res.reason[i][j] == Reason::Lost)
				|| (res.removedAt[j][i] != kNever && res.reason[j][i] == Reason::Lost)) {
				continue;
			}
			const size_t common = std::min(res.hash[i].size(), res.hash[j].size());
			if (!std::equal(res.hash[i].begin(), res.hash[i].begin() + common, res.hash[j].begin())) {
				res.why = "devices " + std::to_string(i) + " and " + std::to_string(j) + " differ in a cycle both executed";
			}
			for (int p = 0; p < n; ++p) {
				if (p != i && p != j && res.removedAt[i][p] != kNever && res.removedAt[j][p] != kNever
					&& (res.removedAt[i][p] != res.removedAt[j][p] || res.reason[i][p] != res.reason[j][p])) {
					res.why = "two devices removed player " + std::to_string(p) + " differently";
				}
			}
		}
	}
	// Liveness: at the end every device is dead, out, or still advancing.
	for (int i = 0; i < n; ++i) {
		if (res.dead[i] || res.out[i]) continue;
		if (res.startedAt[i] < 0 || res.executed[i] <= res.execBefore[i]) res.why = "device " + std::to_string(i) + " is stuck";
	}
	if (res.unsent) res.why = "the model made a message the wire format refuses";
	if (res.unparsed) res.why = "a sent datagram was refused by the parser";
	return res;
}

// The same history on every device in both runs: cycle hashes, removals, times.
static bool SameRun(const Result &a, const Result &b)
{
	if (a.msgs != b.msgs) return false;
	for (int i = 0; i < a.n; ++i) {
		if (a.pauses[i] != b.pauses[i] || a.resumes[i] != b.resumes[i] || a.pauseCycle[i] != b.pauseCycle[i]
			|| a.pausedBy[i] != b.pausedBy[i] || a.resumedBy[i] != b.resumedBy[i]) {
			return false;
		}
		if (a.hash[i] != b.hash[i] || a.executed[i] != b.executed[i] || a.out[i] != b.out[i]
			|| a.outReason[i] != b.outReason[i] || a.startedAt[i] != b.startedAt[i]) {
			return false;
		}
		for (int p = 0; p < a.n; ++p) {
			if (a.removedAt[i][p] != b.removedAt[i][p] || a.reason[i][p] != b.reason[i][p]
				|| a.removedTime[i][p] != b.removedTime[i][p]) {
				return false;
			}
		}
	}
	return true;
}

static bool AllRemoved(const Result &r, uint16_t by, int p, Reason why)
{
	int32_t C = kNever;
	for (int i = 0; i < r.n; ++i) {
		if (!((by >> i) & 1)) continue;
		if (r.removedAt[i][p] == kNever || r.reason[i][p] != why) return false;
		if (C != kNever && C != r.removedAt[i][p]) return false;
		C = r.removedAt[i][p];
	}
	return true;
}

static bool NoneRemoved(const Result &r)
{
	for (int i = 0; i < r.n; ++i) {
		if (r.out[i]) return false;
		for (int p = 0; p < r.n; ++p) {
			if (r.removedAt[i][p] != kNever) return false;
		}
	}
	return true;
}

static bool Advanced(const Result &r, uint16_t who, int32_t atLeast)
{
	for (int i = 0; i < r.n; ++i) {
		if (((who >> i) & 1) && (r.out[i] || r.executed[i] < atLeast)) return false;
	}
	return true;
}

// Runs the configuration over the wire and with structs; the two must not differ.
static Result OverTheWire(const Config &cfg)
{
	const Result wire = Run(cfg, true);
	const Result plain = Run(cfg, false);
	CHECK(wire.why == "");
	CHECK(plain.why == "");
	CHECK(SameRun(wire, plain));
	CHECK(wire.longest <= kWireMaxSize);
	CHECK(wire.msgs > 1000);
	return wire;
}

static Config Base(int n, int64_t endS, uint64_t seed)
{
	Config c;
	c.n = n;
	c.endMs = endS * 1000;
	c.seed = seed;
	return c;
}

TEST_CASE("mesh over the wire: three players, the lowest-numbered one dies")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(3, 100, seed);
		c.faults = {{Fault::Die, 20000, 0, 0}};
		const Result r = OverTheWire(c);
		CHECK(AllRemoved(r, 0x6, 0, Reason::Lost));
		CHECK(Advanced(r, 0x6, 1000));
		CHECK(r.kinds[int(Kind::Status)] > 0);
		CHECK(r.kinds[int(Kind::Commit)] > 0);
		CHECK(r.longest == kWireStatusSize);
	}
}

TEST_CASE("mesh over the wire: the decider dies during the round")
{
	// the moment of the second death moves across the commit of the first, 250 ms a step
	for (int k = 0; k < 40; k += 5) {
		CAPTURE(k);
		Config c = Base(4, 170, 1000 + k);
		c.faults = {{Fault::Die, 20000, 3, 0}, {Fault::Die, 79800 + k * 50, 0, 0}};
		const Result r = OverTheWire(c);
		CHECK(AllRemoved(r, 0x6, 3, Reason::Lost));
		CHECK(AllRemoved(r, 0x6, 0, Reason::Lost));
		CHECK(Advanced(r, 0x6, 700));
	}
}

TEST_CASE("mesh over the wire: a silent player returns at 59 s of 60")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(3, 110, seed);
		c.faults = {{Fault::Silent, 20000, 2, 59000}};
		const Result r = OverTheWire(c);
		CHECK(NoneRemoved(r));
		CHECK(Advanced(r, 0x7, 1400));
	}
}

TEST_CASE("mesh over the wire: the allowance is used up by repeated absences")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(3, 230, seed);
		c.faults = {{Fault::Silent, 20000, 2, 50000}, {Fault::Silent, 80000, 2, 50000}, {Fault::Silent, 140000, 2, 50000}};
		const Result r = OverTheWire(c);
		CHECK(AllRemoved(r, 0x3, 2, Reason::Lost));
		// the third absence is cut at the rest of the allowance (20 s), not at 60 s
		const double after = (r.removedTime[0][2] - 140000) / 1000.0;
		CHECK(after >= 15);
		CHECK(after <= 26);
		CHECK(Advanced(r, 0x3, 3000));
	}
}

TEST_CASE("mesh over the wire: a slow loader holds the start for everybody")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(3, 80, seed);
		c.loadAt = {9000, 12000, 40000};
		const Result r = OverTheWire(c);
		const int64_t lo = std::min({r.startedAt[0], r.startedAt[1], r.startedAt[2]});
		const int64_t hi = std::max({r.startedAt[0], r.startedAt[1], r.startedAt[2]});
		CHECK(lo >= 40000);
		CHECK(hi - lo <= 1000);
		CHECK(NoneRemoved(r));
		CHECK(Advanced(r, 0x7, 1000));
		CHECK(r.kinds[int(Kind::Loading)] > 0);
		CHECK(r.kinds[int(Kind::Loaded)] > 0);
	}
}

TEST_CASE("mesh over the wire: a deliberate quit")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(3, 60, seed);
		c.faults = {{Fault::Quit, 20000, 1, 0}};
		const Result r = OverTheWire(c);
		CHECK(AllRemoved(r, 0x5, 1, Reason::Left));
		CHECK(r.removedTime[0][1] <= 22000); // known within 2 s of the click, no wait
		CHECK(r.out[1]);
		CHECK(r.outReason[1] == Reason::Left);
		CHECK(Advanced(r, 0x5, 1100));
	}
}

TEST_CASE("mesh over the wire: a deliberate quit, four players, 30 % loss")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(4, 120, seed);
		c.loss = 0.30;
		c.faults = {{Fault::Quit, 20000, 2, 0}};
		const Result r = OverTheWire(c);
		CHECK(r.removedAt[0][2] != kNever);
		CHECK(r.removedAt[1][2] == r.removedAt[0][2]);
		CHECK(r.removedAt[3][2] == r.removedAt[0][2]);
		CHECK(Advanced(r, 0xB, 1500));
		CHECK(r.kinds[int(Kind::Need)] > 0);
	}
}

TEST_CASE("mesh over the wire: eight players, 20 % loss")
{
	Config c = Base(8, 120, 1000);
	c.loss = 0.20;
	const Result r = OverTheWire(c);
	CHECK(NoneRemoved(r));
	CHECK(Advanced(r, 0xFF, 3300)); // full speed would be 3600
	MESSAGE("eight players: " << r.msgs << " datagrams delivered, " << r.bytes << " bytes, slowest device at cycle "
		<< *std::min_element(r.executed, r.executed + 8));
}

TEST_CASE("mesh over the wire: a pause by one player, the resume by another, 20 % loss")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(4, 160, seed);
		c.loss = 0.20;
		c.dmax = 150;
		c.faults = {{Fault::Pause, 20000, 0, 0}, {Fault::Resume, 110000, 2, 0}};
		const Result r = OverTheWire(c);
		CHECK(NoneRemoved(r)); // 90 s of pause are no silence
		for (int i = 0; i < 4; ++i) {
			CAPTURE(i);
			CHECK(r.pauses[i] == 1);
			CHECK(r.resumes[i] == 1);
			CHECK(r.paused[i] == false);
			CHECK(r.pauseCycle[i] == r.pauseCycle[0]);
			CHECK(r.pausedBy[i] == 0);
		}
		CHECK(r.pauseCycle[0] > 580);
		CHECK(r.pauseCycle[0] < 640);
		CHECK(Advanced(r, 0xF, 1500));
		CHECK(r.kinds[int(Kind::Pause)] > 4 * 3 * 250); // four a second from every device to every other, less the loss
	}
}

TEST_CASE("mesh over the wire: a device dies during a pause")
{
	for (uint64_t seed = 1000; seed < 1003; ++seed) {
		CAPTURE(seed);
		Config c = Base(3, 160, seed);
		c.faults = {{Fault::Pause, 20000, 1, 0}, {Fault::Die, 30000, 2, 0}, {Fault::Resume, 120000, 0, 0}};
		const Result r = OverTheWire(c);
		CHECK(AllRemoved(r, 0x3, 2, Reason::Lost));
		CHECK(r.removedTime[0][2] >= 89000);  // its 60 s, counted on real time
		CHECK(r.removedTime[0][2] < 100000);  // found while the game stood
		CHECK(r.removedAt[0][2] > r.pauseCycle[0]); // a cycle that has not run
		CHECK(r.pauses[0] == 1);
		CHECK(r.resumes[1] == 1);
		CHECK(r.resumedBy[1] == 0);
		CHECK(Advanced(r, 0x3, 1500));
	}
}

// ------------------------------------------------------------------------------------------------
// The pause of the model alone (NET_PAUSE_SPEC.md): devices on a network that loses nothing and
// delivers at once, so that every step is known. `drop` takes single messages away.

struct PauseRig {
	explicit PauseRig(int n, const Params &params = Params()) : n(n)
	{
		for (int i = 0; i < n; ++i) {
			dev.emplace_back(new Mesh(n, i, params, 0));
			dev[i]->OnLoaded(0);
		}
		Run(200);
	}
	// Everything the devices have to say reaches the others, answers included.
	void Deliver()
	{
		for (int round = 0; round < 8; ++round) {
			for (int i = 0; i < n; ++i) {
				std::vector<Out> out;
				out.swap(dev[i]->out);
				for (const Event &e : dev[i]->events) seen[i].push_back(e);
				dev[i]->events.clear();
				for (const Out &o : out) {
					if (mute[i] || mute[o.to] || (drop && drop(i, o))) continue;
					dev[o.to]->OnMessage(i, o.msg, now);
				}
			}
		}
	}
	// 10 ms a step; every device runs one cycle a step if it may.
	void Run(int ms)
	{
		for (int t = 0; t < ms; t += 10) {
			now += 10;
			for (int i = 0; i < n; ++i) {
				if (mute[i]) continue;
				dev[i]->Tick(now);
				if (dev[i]->CanExecute(dev[i]->Executed() + 1)) dev[i]->OnExecuted(dev[i]->Executed() + 1);
			}
			Deliver();
		}
	}
	// The pause command of `by` ran in the last cycle of every device.
	void PauseAll(int by)
	{
		for (int i = 0; i < n; ++i) {
			if (!mute[i]) taken[i] = dev[i]->Pause(by, now);
		}
		Deliver();
	}
	int Count(int i, Event::Type type) const
	{
		int c = 0;
		for (const Event &e : seen[i]) c += e.type == type ? 1 : 0;
		return c;
	}
	const Event *Last(int i, Event::Type type) const
	{
		const Event *res = nullptr;
		for (const Event &e : seen[i]) {
			if (e.type == type) res = &e;
		}
		return res;
	}

	int n;
	int64_t now = 0;
	std::vector<std::unique_ptr<Mesh>> dev;
	std::vector<Event> seen[kMaxPlayers];
	bool mute[kMaxPlayers] = {};
	bool taken[kMaxPlayers] = {};
	std::function<bool(int from, const Out &o)> drop;
};

TEST_CASE("mesh pause: every device stands after the same cycle, nobody is silent, another player resumes")
{
	PauseRig rig(3);
	const int32_t P = rig.dev[0]->Executed();
	REQUIRE(P > 10);
	REQUIRE(rig.dev[1]->Executed() == P);
	REQUIRE(rig.dev[2]->Executed() == P);
	rig.PauseAll(1);
	for (int i = 0; i < 3; ++i) {
		CAPTURE(i);
		CHECK(rig.taken[i]);
		CHECK(rig.dev[i]->IsPaused());
		CHECK(rig.dev[i]->PausedBy() == 1);
		CHECK(rig.dev[i]->PauseCycle() == P);
		CHECK_FALSE(rig.dev[i]->CanExecute(P + 1));
		REQUIRE(rig.Last(i, Event::Paused) != nullptr);
		CHECK(rig.Last(i, Event::Paused)->player == 1);
		CHECK(rig.Last(i, Event::Paused)->cycle == P);
	}
	// 150 s, far beyond the bound of an absence and beyond the whole allowance
	rig.Run(150000);
	for (int i = 0; i < 3; ++i) {
		CAPTURE(i);
		CHECK(rig.dev[i]->IsPaused());
		CHECK(rig.dev[i]->Executed() == P);
		CHECK(rig.Count(i, Event::Waiting) == 0);
		CHECK(rig.Count(i, Event::PlayerRemoved) == 0);
		CHECK(rig.Count(i, Event::SelfOut) == 0);
		for (int p = 0; p < 3; ++p) {
			if (p == i) continue;
			CHECK_FALSE(rig.dev[i]->IsQuiet(p, rig.now));
			CHECK_FALSE(rig.dev[i]->IsSuspected(p));
			CHECK(rig.dev[i]->AllowanceLeft(p, rig.now) == Params().allowanceMs);
		}
	}
	rig.dev[2]->Resume(rig.now);
	rig.Deliver();
	for (int i = 0; i < 3; ++i) {
		CAPTURE(i);
		CHECK_FALSE(rig.dev[i]->IsPaused());
		REQUIRE(rig.Last(i, Event::Resumed) != nullptr);
		CHECK(rig.Last(i, Event::Resumed)->player == 2);
		CHECK(rig.Last(i, Event::Resumed)->cycle == P);
		CHECK(rig.dev[i]->CanExecute(P + 1));
	}
	rig.Run(1000);
	for (int i = 0; i < 3; ++i) {
		CHECK(rig.dev[i]->Executed() == P + 100);
		CHECK(rig.Count(i, Event::Paused) == 1);
		CHECK(rig.Count(i, Event::Resumed) == 1);
	}
}

TEST_CASE("mesh pause: a lost resume is repaired by the answer to the heartbeat")
{
	PauseRig rig(3);
	rig.PauseAll(0);
	rig.Run(1000);
	const int32_t P = rig.dev[0]->Executed();
	// device 0 gets no command packet, and nothing of device 2 but its sign of life
	rig.drop = [](int from, const Out &o) {
		return o.to == 0 && (o.msg.kind == Kind::Cmd || (from == 2 && !(o.msg.kind == Kind::Pause && (o.msg.flags & 1))));
	};
	rig.dev[2]->Resume(rig.now);
	rig.Deliver();
	CHECK_FALSE(rig.dev[1]->IsPaused());
	CHECK(rig.dev[0]->IsPaused());
	// device 1 knows that the pause is over: it answers the heartbeat of device 0
	rig.Run(300);
	CHECK_FALSE(rig.dev[0]->IsPaused());
	REQUIRE(rig.Last(0, Event::Resumed) != nullptr);
	CHECK(rig.Last(0, Event::Resumed)->player == 2);
	rig.drop = nullptr;
	rig.Run(1000);
	for (int i = 0; i < 3; ++i) CHECK(rig.dev[i]->Executed() > P + 80);
}

TEST_CASE("mesh pause: a lost resume is repaired by the packets of a device that goes on")
{
	PauseRig rig(2);
	rig.PauseAll(0);
	rig.Run(1000);
	const int32_t P = rig.dev[0]->Executed();
	// no pause message at all gets through any more
	rig.drop = [](int, const Out &o) { return o.msg.kind == Kind::Pause; };
	rig.dev[1]->Resume(rig.now);
	rig.Deliver();
	CHECK(rig.dev[0]->IsPaused());
	rig.Run(100);
	CHECK_FALSE(rig.dev[0]->IsPaused());
	REQUIRE(rig.Last(0, Event::Resumed) != nullptr);
	CHECK(rig.Last(0, Event::Resumed)->player == 1); // the one whose packet showed it
	rig.Run(1000);
	CHECK(rig.dev[0]->Executed() > P + 80);
	CHECK(rig.dev[1]->Executed() > P + 80);
}

TEST_CASE("mesh pause: a press that crossed a pause is ignored on every device, a later one is taken")
{
	PauseRig rig(3);
	rig.PauseAll(0);
	const int32_t P = rig.dev[0]->Executed();
	// the second command of the same cycle
	rig.PauseAll(1);
	for (int i = 0; i < 3; ++i) {
		CHECK_FALSE(rig.taken[i]);
		CHECK(rig.dev[i]->PausedBy() == 0);
	}
	rig.dev[1]->Resume(rig.now);
	rig.Deliver();
	// every cycle a command pressed before the pause can lie in
	for (int32_t c = P + 1; c <= P + kLag + 1; ++c) {
		rig.Run(10);
		REQUIRE(rig.dev[0]->Executed() == c);
		rig.PauseAll(2);
		for (int i = 0; i < 3; ++i) {
			CHECK_FALSE(rig.taken[i]);
			CHECK_FALSE(rig.dev[i]->IsPaused());
		}
	}
	rig.Run(10);
	rig.PauseAll(2);
	for (int i = 0; i < 3; ++i) {
		CHECK(rig.taken[i]);
		CHECK(rig.dev[i]->IsPaused());
		CHECK(rig.dev[i]->PausedBy() == 2);
		CHECK(rig.dev[i]->PauseCycle() == P + kLag + 2);
	}
}

TEST_CASE("mesh pause: none before the game has begun, none on a device that left, no resume without a pause")
{
	Mesh held(3, 0, Params(), 0);
	CHECK_FALSE(held.Pause(1, 0));
	held.OnLoaded(0);
	held.Tick(10);
	CHECK_FALSE(held.Pause(1, 10)); // loaded, but the others have not reported
	CHECK_FALSE(held.IsPaused());

	PauseRig rig(3);
	rig.dev[0]->Resume(rig.now);
	rig.Deliver();
	CHECK(rig.Count(1, Event::Resumed) == 0);
	CHECK_FALSE(rig.dev[1]->Pause(3, rig.now)); // a player that is none
	rig.dev[2]->Quit(rig.now);
	CHECK_FALSE(rig.dev[2]->Pause(0, rig.now));
	CHECK_FALSE(rig.dev[2]->IsPaused());
}

TEST_CASE("mesh pause: a device that is behind does not begin a pause the others have ended")
{
	PauseRig rig(2);
	const int32_t e = rig.dev[0]->Executed();
	Msg over;
	over.kind = Kind::Pause;
	over.player = 1;
	// beyond what any device can have reached: not believed
	over.cycle = e + kLag + 3;
	rig.dev[0]->OnMessage(1, over, rig.now);
	// the pause after the cycle this device runs next is over
	over.cycle = e + 1;
	rig.dev[0]->OnMessage(1, over, rig.now);
	rig.Run(10);
	REQUIRE(rig.dev[0]->Executed() == e + 1);
	CHECK(rig.dev[0]->Pause(1, rig.now)); // taken for the rule of the crossing press
	CHECK_FALSE(rig.dev[0]->IsPaused());  // but the game goes on
	CHECK(rig.Count(0, Event::Paused) == 0);
	rig.Run(10 * (kLag + 2));
	REQUIRE(rig.dev[0]->Executed() == e + kLag + 3);
	CHECK(rig.dev[0]->Pause(1, rig.now));
	CHECK(rig.dev[0]->IsPaused()); // the message that was not believed ended nothing
}

TEST_CASE("mesh pause: a device that dies during a pause is waited for and removed as ever")
{
	Params params;
	params.dropMs = 6000;
	params.allowanceMs = 12000;
	params.plateMs = 500;
	params.hearMs = 1000;
	PauseRig rig(3, params);
	rig.PauseAll(0);
	const int32_t P = rig.dev[0]->Executed();
	rig.Run(2000);
	rig.mute[2] = true;
	rig.Run(1000);
	CHECK(rig.Count(0, Event::Waiting) > 0); // the plate
	CHECK(rig.Last(0, Event::Waiting)->player == 2);
	CHECK(rig.dev[0]->IsQuiet(2, rig.now));
	CHECK_FALSE(rig.dev[0]->IsQuiet(1, rig.now));
	rig.Run(8000);
	for (int i = 0; i < 2; ++i) {
		CAPTURE(i);
		REQUIRE(rig.Last(i, Event::PlayerRemoved) != nullptr);
		CHECK(rig.Last(i, Event::PlayerRemoved)->player == 2);
		CHECK(rig.Last(i, Event::PlayerRemoved)->reason == Reason::Lost);
		CHECK(rig.Last(i, Event::PlayerRemoved)->cycle > P); // a cycle that has not run
		CHECK(rig.dev[i]->IsPaused());                       // the removal ends no pause
		CHECK(rig.dev[i]->Executed() == P);
	}
	CHECK(rig.dev[0]->RemovedAt(2) == rig.dev[1]->RemovedAt(2));
	rig.dev[1]->Resume(rig.now);
	rig.Run(1000);
	CHECK(rig.dev[0]->Executed() > P + 80);
	CHECK(rig.dev[1]->Executed() > P + 80);
}

TEST_CASE("mesh pause: a leave during a pause ends no pause")
{
	PauseRig rig(3);
	rig.PauseAll(0);
	const int32_t P = rig.dev[0]->Executed();
	rig.Run(500);
	rig.dev[0]->Quit(rig.now);
	rig.Run(3000);
	for (int i = 1; i < 3; ++i) {
		CAPTURE(i);
		CHECK(rig.dev[i]->IsPaused());
		CHECK(rig.dev[i]->RemovedAt(0) != kNever);
		CHECK(rig.dev[i]->RemovedWhy(0) == Reason::Left);
	}
	rig.dev[2]->Resume(rig.now);
	rig.Run(1000);
	CHECK(rig.dev[1]->Executed() > P + 80);
	CHECK(rig.dev[2]->Executed() > P + 80);
}

} // namespace wire_test
} // namespace mesh
