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
/**@name test_net_message.cpp - The test file for net_message.cpp serialization. */
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
#include "net_message.h"
#include "net_serialization.h"

#include <cstring>
#include <string>
#include <vector>

TEST_CASE("serialize16/32 are big-endian on the wire and round-trip")
{
	unsigned char buf[16];
	serialize16(buf, uint16_t(0xABCD));
	CHECK(buf[0] == 0xAB);
	CHECK(buf[1] == 0xCD);
	uint16_t u16 = 0;
	deserialize16(buf, &u16);
	CHECK(u16 == 0xABCD);

	serialize32(buf, uint32_t(0x11223344));
	CHECK(buf[0] == 0x11);
	CHECK(buf[3] == 0x44);
	uint32_t u32 = 0;
	deserialize32(buf, &u32);
	CHECK(u32 == 0x11223344u);
}

TEST_CASE("string serialize leaves no uninitialised bytes on the wire")
{
	unsigned char buf[64];
	std::memset(buf, 0xAA, sizeof buf);
	const std::string s = "hello-lan";
	const size_t adv = serialize(buf, s);
	std::string got;
	const size_t adv2 = deserialize(buf, got);
	CHECK(got == s);
	CHECK(adv == adv2);
	// Every byte the reported length covers must have been written (no 0xAA left over).
	for (size_t i = 0; i < adv; ++i) {
		CHECK(buf[i] != 0xAA);
	}
}

TEST_CASE("empty vector serialize/deserialize advance consistently")
{
	// Regression: an empty vector field must advance the writer, the null-buffer sizing
	// path and the reader by the same amount, or a mid-message empty vector desyncs.
	unsigned char buf[32];
	std::vector<unsigned char> empty;
	const size_t write = serialize(buf, empty);
	const size_t size = serialize(nullptr, empty);
	std::vector<unsigned char> got{ 9 };
	const size_t read = deserialize(buf, got);
	CHECK(write == read);
	CHECK(write == size);
	CHECK(got.empty());
}

TEST_CASE("message Serialize(nullptr) reports Size() without dereferencing")
{
	CInitMessage_Header h(7, 3);
	CHECK(h.Serialize(nullptr) == CInitMessage_Header::Size());

	CNetworkHost nh;
	nh.SetName("player");
	CHECK(nh.Serialize(nullptr) == CNetworkHost::Size());

	CServerSetup ss;
	CHECK(ss.Serialize(nullptr) == CServerSetup::Size());

	CNetworkCommand nc;
	CHECK(nc.Serialize(nullptr) == CNetworkCommand::Size());
}

TEST_CASE("CNetworkHost round-trips every field")
{
	unsigned char buf[128];
	CNetworkHost a;
	a.Host = 0xC0A80101u;
	a.Port = 6112;
	a.PlyNr = 4;
	a.SetName("PlayerOne");

	const size_t wrote = a.Serialize(buf);
	CNetworkHost b;
	const size_t read = b.Deserialize(buf);
	CHECK(wrote == read);
	CHECK(wrote == CNetworkHost::Size());
	CHECK(b.Host == a.Host);
	CHECK(b.Port == a.Port);
	CHECK(b.PlyNr == a.PlyNr);
	CHECK(std::strcmp(a.PlyName, b.PlyName) == 0);
}

TEST_CASE("CServerSetup round-trips distinct per-player values")
{
	unsigned char buf[4096];
	CServerSetup a;
	auto &s = a.ServerGameSettings;
	s.Resources = 3;
	s.NumUnits = 5;
	s.Opponents = 7;
	s.NoFogOfWar = 1;
	for (int i = 0; i < PlayerMax; ++i) {
		auto &pr = s.Presets[i];
		pr.PlayerColor = int8_t(i + 1);
		pr.Race = int8_t(i * 2 + 1);
		pr.Team = int8_t(i % 4);
		char nm[8] = { 'a', 'i', char('0' + (i % 10)), 0, 0, 0, 0, 0 };
		pr.AIScript = nm;
		a.CompOpt[i] = static_cast<SlotOption>(i % 3);
		a.Ready[i] = uint8_t(i % 2);
	}

	const size_t wrote = a.Serialize(buf);
	CServerSetup b;
	const size_t read = b.Deserialize(buf);
	CHECK(wrote == read);
	CHECK(wrote == CServerSetup::Size());
	bool ok = int(b.ServerGameSettings.NoFogOfWar) == 1;
	for (int i = 0; i < PlayerMax; ++i) {
		const auto &pa = a.ServerGameSettings.Presets[i];
		const auto &pb = b.ServerGameSettings.Presets[i];
		ok = ok && pa.PlayerColor == pb.PlayerColor && pa.Race == pb.Race
		     && pa.Team == pb.Team && pa.AIScript == pb.AIScript
		     && a.CompOpt[i] == b.CompOpt[i] && a.Ready[i] == b.Ready[i];
	}
	CHECK(ok);
}

TEST_CASE("CNetworkPacket frames commands including an empty one")
{
	// Regression for the empty-vector desync: a packet whose command list contains an
	// empty payload must still report a matching size and recover the right count.
	unsigned char buf[8192];
	CNetworkPacket a;
	for (int i = 0; i < MaxNetworkCommands; ++i) {
		a.Header.Type[i] = uint8_t(i + 1);
	}
	a.Header.Cycle = 200;
	a.Header.OrigPlayer = 6;
	const int n = 4;
	a.Command[0] = { 1, 2, 3, 4, 5 };
	a.Command[1] = {}; // empty command
	a.Command[2] = { 0xFF };
	a.Command[3] = { 7, 7, 7 };

	const size_t sz = a.Size(n);
	const size_t wrote = a.Serialize(buf, n);
	CHECK(sz == wrote);

	CNetworkPacket b;
	int count = -1;
	b.Deserialize(buf, (unsigned int) wrote, &count);
	CHECK(count == n);
	for (int i = 0; i < n; ++i) {
		CHECK(a.Command[i] == b.Command[i]);
	}
	CHECK(b.Header.Cycle == 200);
	CHECK(b.Header.OrigPlayer == 6);
}
