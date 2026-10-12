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
/**@name test_net_mesh_glue.cpp - The test file for net_mesh_glue.cpp. */
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
#include "net_mesh_glue.h"

#include <string>
#include <tuple>
#include <vector>

//
// The glue alone: a recording engine side, a clock of the test, no socket, no global.
// Host slots 0, 1, 2 are the players 4, 2, 7, so that a slot taken for a player shows.
//

namespace {

const int SlotPlayer[3] = {4, 2, 7};

class RecordingEngine : public CMeshGlueEngine
{
public:
	void SendMesh(int hostSlot, const mesh::Msg &message) override { meshes.emplace_back(hostSlot, message); }
	void SendPacket(int hostSlot, int player, unsigned long cycle) override
	{
		packets.emplace_back(hostSlot, player, cycle);
	}
	void InstallQuit(int player, unsigned long cycle, mesh::Reason reason) override
	{
		quits.emplace_back(player, cycle, reason);
	}
	void SetPaused(bool paused, int player) override { pauses.emplace_back(paused, player); }
	void SendChat(int hostSlot, uint32_t number, const std::string &text) override
	{
		chatsSent.emplace_back(hostSlot, number, text);
	}
	void ShowChat(int player, const std::string &text) override { chatsShown.emplace_back(player, text); }

	/// Number of messages of a kind sent to a host slot
	int Count(mesh::Kind kind, int hostSlot) const
	{
		int n = 0;
		for (const auto &[to, message] : meshes) {
			n += (to == hostSlot && message.kind == kind) ? 1 : 0;
		}
		return n;
	}
	/// The last message of a kind sent to a host slot, null if none
	const mesh::Msg *Last(mesh::Kind kind, int hostSlot) const
	{
		const mesh::Msg *res = nullptr;
		for (const auto &[to, message] : meshes) {
			if (to == hostSlot && message.kind == kind) {
				res = &message;
			}
		}
		return res;
	}

	std::vector<std::pair<int, mesh::Msg>> meshes;
	std::vector<std::tuple<int, int, unsigned long>> packets;
	std::vector<std::tuple<int, unsigned long, mesh::Reason>> quits;
	std::vector<std::pair<bool, int>> pauses;
	std::vector<std::tuple<int, uint32_t, std::string>> chatsSent;
	std::vector<std::pair<int, std::string>> chatsShown;
};

mesh::Params ShortBounds()
{
	mesh::Params params;

	params.dropMs = 600;
	params.allowanceMs = 1000;
	params.exhaustedDropMs = 150;
	params.plateMs = 100;
	params.hearMs = 200;
	params.statusMs = 50;
	params.needMs = 20;
	params.relayAfterMs = 60;
	params.loadMs = 1200;
	params.loadPingDropMs = 400;
	params.loadedMs = 50;
	params.lingerQuietMs = 100;
	params.lingerCapMs = 400;
	return params;
}

class GlueRig
{
public:
	/// With `start` every device has loaded and the game runs; without it the game is held
	explicit GlueRig(int ownSlot, bool start = true) :
		own(ownSlot),
		glue(engine, std::vector<int>(SlotPlayer, SlotPlayer + 3), ownSlot, ShortBounds(), now)
	{
		if (start) {
			glue.OnLoaded(now);
			for (int slot = 0; slot != 3; ++slot) {
				if (slot != own) {
					glue.OnMeshMessage(slot, LoadedBy(0x7), now);
				}
			}
			glue.Tick(now);
			REQUIRE(glue.Held() == false);
			engine.meshes.clear();
		}
	}

	/// The report of a device which has loaded and knows the devices in `seen` as loaded
	static mesh::Msg LoadedBy(uint16_t seen)
	{
		mesh::Msg loaded;
		loaded.kind = mesh::Kind::Loaded;
		loaded.mask = seen;
		return loaded;
	}

	/// The device in a host slot sends its packet of a cycle
	void Packet(int hostSlot, unsigned long cycle, bool quit = false)
	{
		glue.OnPacket(hostSlot, SlotPlayer[hostSlot], cycle, quit, now);
	}
	/// Run the cycles up to `last`; the others in `senders` deliver theirs in time
	void RunTo(unsigned long last, std::initializer_list<int> senders)
	{
		for (unsigned long cycle = glue.Model().Executed() + 1; cycle <= last; ++cycle) {
			for (int slot : senders) {
				Packet(slot, cycle);
			}
			REQUIRE(glue.CanExecute(cycle));
			glue.OnExecuted(cycle, now);
		}
	}
	/// Let `ms` pass in steps of 10; the devices in `alive` keep talking
	void Pass(int ms, std::initializer_list<int> alive)
	{
		for (int t = 0; t < ms; t += 10) {
			now += 10;
			for (int slot : alive) {
				glue.OnHeard(slot, now);
			}
			glue.Tick(now);
		}
	}
	/// The view the device in `hostSlot` would send if it saw what this device sees
	mesh::Msg SameView(int hostSlot)
	{
		const mesh::Msg *mine = engine.Last(mesh::Kind::Status, hostSlot);
		REQUIRE(mine != nullptr);
		mesh::Msg view = *mine;
		view.player = uint8_t(hostSlot);
		view.data = ++sequence;
		view.want = 0;
		return view;
	}
	mesh::Msg Commit(int hostSlot, unsigned long cycle, uint16_t alive, mesh::Reason reason = mesh::Reason::Lost) const
	{
		mesh::Msg commit;
		commit.kind = mesh::Kind::Commit;
		commit.player = uint8_t(hostSlot);
		commit.cycle = int32_t(cycle);
		commit.mask = alive;
		commit.flags = uint8_t(reason);
		return commit;
	}

	const int own;
	int64_t now = 1000;
	uint32_t sequence = 0;
	RecordingEngine engine;
	CMeshGlue glue;
};

}

TEST_CASE("mesh glue: host slot and player number")
{
	GlueRig rig(1);

	CHECK(rig.glue.PlayerOfSlot(0) == 4);
	CHECK(rig.glue.PlayerOfSlot(1) == 2);
	CHECK(rig.glue.PlayerOfSlot(2) == 7);
	CHECK(rig.glue.PlayerOfSlot(3) == -1);
	CHECK(rig.glue.PlayerOfSlot(-1) == -1);
	CHECK(rig.glue.SlotOfPlayer(4) == 0);
	CHECK(rig.glue.SlotOfPlayer(2) == 1);
	CHECK(rig.glue.SlotOfPlayer(7) == 2);
	CHECK(rig.glue.SlotOfPlayer(0) == -1);
	CHECK(rig.glue.SlotOfPlayer(1) == -1);
}

TEST_CASE("mesh glue: a cycle runs when the queue holds the packet of every player")
{
	GlueRig rig(1);

	// the cycles 0..lag are held from the start, cycle 0 is done
	CHECK(rig.glue.CanExecute(1));
	CHECK(rig.glue.CanExecute(2) == false); // not the next one
	rig.RunTo(mesh::kLag, {});
	CHECK(rig.glue.CanExecute(mesh::kLag + 1) == false);
	rig.Packet(0, mesh::kLag + 1);
	CHECK(rig.glue.CanExecute(mesh::kLag + 1) == false);
	rig.Packet(2, mesh::kLag + 1);
	CHECK(rig.glue.CanExecute(mesh::kLag + 1));
	// a packet of a player who has no host slot, or from a device which has none, is no fact
	rig.glue.OnPacket(0, 0, mesh::kLag + 2, false, rig.now);
	rig.glue.OnPacket(5, 7, mesh::kLag + 2, false, rig.now);
	rig.glue.OnExecuted(mesh::kLag + 1, rig.now);
	rig.Packet(0, mesh::kLag + 2);
	CHECK(rig.glue.CanExecute(mesh::kLag + 2) == false);
	// the own fresh packets are the engine's to send: the glue sent nothing at all so far
	CHECK(rig.engine.packets.empty());
	CHECK(rig.engine.meshes.empty());
	CHECK(rig.engine.quits.empty());
}

TEST_CASE("mesh glue: the command of the model never comes over the wire")
{
	GlueRig rig(1);

	rig.RunTo(mesh::kLag, {});
	mesh::Msg command;
	command.kind = mesh::Kind::Cmd;
	command.cycle = mesh::kLag + 1;
	command.player = 0;
	rig.glue.OnMeshMessage(0, command, rig.now);
	command.player = 2;
	rig.glue.OnMeshMessage(2, command, rig.now);
	CHECK(rig.glue.CanExecute(mesh::kLag + 1) == false);
}

TEST_CASE("mesh glue: a missing packet is asked for, first its player, then everybody")
{
	GlueRig rig(1);

	rig.RunTo(mesh::kLag, {});
	rig.Packet(0, mesh::kLag + 1);
	rig.Packet(2, mesh::kLag);       // heard once, so its clock runs
	rig.engine.meshes.clear();

	rig.Pass(50, {0, 2});
	const mesh::Msg *need = rig.engine.Last(mesh::Kind::Need, 2);
	REQUIRE(need != nullptr);
	CHECK(need->cycle == mesh::kLag + 1);
	CHECK(need->mask == (1u << 2)); // the host slot, not the player number 7
	CHECK(need->flags == 0);
	// at most one request per 20 ms and device
	CHECK(rig.engine.Count(mesh::Kind::Need, 2) <= 3);
	CHECK(rig.engine.Count(mesh::Kind::Need, 0) == rig.engine.Count(mesh::Kind::Need, 2));

	rig.Pass(50, {0, 2});
	need = rig.engine.Last(mesh::Kind::Need, 0);
	REQUIRE(need != nullptr);
	CHECK(need->cycle == mesh::kLag + 1);
	CHECK(need->mask == (1u << 2));
	CHECK(need->flags == 1); // stalled for 60 ms: any device which holds it may answer
	CHECK(rig.engine.packets.empty());
	CHECK(rig.engine.quits.empty());

	// it arrives by the other device: the cycle runs
	rig.glue.OnPacket(0, 7, mesh::kLag + 1, false, rig.now);
	CHECK(rig.glue.CanExecute(mesh::kLag + 1));
	// he is still not heard himself, but nobody waits for him while his packets come that way
	rig.Pass(300, {0});
	CHECK(rig.glue.WaitState(rig.now).Player == -1);
	rig.glue.OnExecuted(mesh::kLag + 1, rig.now);
	rig.Packet(0, mesh::kLag + 2);
	rig.Pass(300, {0});
	CHECK(rig.glue.WaitState(rig.now).Player == 7);
	CHECK(rig.engine.quits.empty());
}

TEST_CASE("mesh glue: a request is answered to the asker with the packets held")
{
	GlueRig rig(1);

	rig.RunTo(mesh::kLag + 2, {0, 2});
	rig.engine.packets.clear();

	mesh::Msg need;
	need.kind = mesh::Kind::Need;
	need.cycle = mesh::kLag;
	need.mask = uint16_t(1u << 1); // this device's packets
	rig.glue.OnMeshMessage(0, need, rig.now);
	REQUIRE(rig.engine.packets.size() >= 3);
	for (size_t i = 0; i != rig.engine.packets.size(); ++i) {
		CHECK(std::get<0>(rig.engine.packets[i]) == 0);                 // to the asker only
		CHECK(std::get<1>(rig.engine.packets[i]) == 2);                 // this device's player number
		CHECK(std::get<2>(rig.engine.packets[i]) == mesh::kLag + i);    // from the asked cycle on
	}

	// the packets of another player only when the asker says so
	rig.engine.packets.clear();
	need.mask = uint16_t(1u << 2);
	rig.glue.OnMeshMessage(0, need, rig.now);
	CHECK(rig.engine.packets.empty());
	need.flags = 1;
	rig.glue.OnMeshMessage(0, need, rig.now);
	REQUIRE(rig.engine.packets.size() == 3); // lag, lag + 1, lag + 2
	for (size_t i = 0; i != rig.engine.packets.size(); ++i) {
		CHECK(std::get<0>(rig.engine.packets[i]) == 0);
		CHECK(std::get<1>(rig.engine.packets[i]) == 7);
		CHECK(std::get<2>(rig.engine.packets[i]) == mesh::kLag + i);
	}
	// nobody gets his own packets back
	rig.engine.packets.clear();
	rig.glue.OnMeshMessage(2, need, rig.now);
	CHECK(rig.engine.packets.empty());
}

TEST_CASE("mesh glue: a silent player is waited for, then removed at the cycle of the commit")
{
	GlueRig rig(1);
	const unsigned long stall = mesh::kLag + 3;

	rig.RunTo(stall - 1, {0, 2});
	rig.Packet(0, stall);
	CHECK(rig.glue.WaitState(rig.now).Player == -1);

	// 3 s (here 100 ms) of silence: he is named, the time runs down
	rig.Pass(200, {0});
	const CNetworkWaitState first = rig.glue.WaitState(rig.now);
	CHECK(first.Player == 7);
	CHECK(first.Removing == false);
	CHECK(first.MsLeft > 300);
	CHECK(first.MsLeft <= 500);
	CHECK(first.AllowanceMsLeft <= 1000 - 200);
	CHECK(first.AllowanceMsLeft > 0);
	rig.Pass(200, {0});
	const CNetworkWaitState second = rig.glue.WaitState(rig.now);
	CHECK(second.Player == 7);
	CHECK(second.MsLeft < first.MsLeft);
	CHECK(second.AllowanceMsLeft < first.AllowanceMsLeft);
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.CanExecute(stall) == false);
	CHECK(rig.glue.Accepts(2));

	// the bound: frozen, the view goes to the others, nothing is removed by the own clock
	rig.Pass(300, {0});
	const CNetworkWaitState third = rig.glue.WaitState(rig.now);
	CHECK(third.Player == 7);
	CHECK(third.Removing);
	CHECK(rig.glue.Accepts(2) == false);
	const mesh::Msg *status = rig.engine.Last(mesh::Kind::Status, 0);
	REQUIRE(status != nullptr);
	CHECK(status->mask == 0x3); // host slots 0 and 1 alive
	CHECK(status->have[2] == int32_t(stall - 1));
	rig.Pass(500, {0});
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.CanExecute(stall) == false);

	// the lowest device decides
	rig.glue.OnMeshMessage(0, rig.Commit(2, stall, 0x3), rig.now);
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<0>(rig.engine.quits[0]) == 7); // the player number
	CHECK(std::get<1>(rig.engine.quits[0]) == stall);
	CHECK(std::get<2>(rig.engine.quits[0]) == mesh::Reason::Lost);
	CHECK(rig.glue.CanExecute(stall));
	CHECK(rig.glue.WaitState(rig.now).Player == -1);
	// the same commit again changes nothing
	rig.glue.OnMeshMessage(0, rig.Commit(2, stall, 0x3), rig.now);
	CHECK(rig.engine.quits.size() == 1);
	// the game goes on with two
	rig.RunTo(stall + 5, {0});
}

TEST_CASE("mesh glue: the lowest device decides the cycle itself")
{
	GlueRig rig(0);
	const unsigned long stall = mesh::kLag + 3;

	rig.RunTo(stall - 1, {1, 2});
	rig.Packet(1, stall);
	rig.Pass(700, {1});
	REQUIRE(rig.glue.WaitState(rig.now).Removing);
	// alone it decides nothing: the other device has not said what it sees
	rig.Pass(300, {1});
	CHECK(rig.engine.quits.empty());
	CHECK(rig.engine.Count(mesh::Kind::Commit, 1) == 0);

	rig.glue.OnMeshMessage(1, rig.SameView(1), rig.now);
	rig.Pass(20, {1});
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<0>(rig.engine.quits[0]) == 7);
	CHECK(std::get<1>(rig.engine.quits[0]) == stall); // the first cycle nobody holds his packet of
	const mesh::Msg *commit = rig.engine.Last(mesh::Kind::Commit, 1);
	REQUIRE(commit != nullptr);
	CHECK(commit->player == 2); // the host slot on the wire
	CHECK(commit->cycle == int32_t(stall));
	CHECK(commit->mask == 0x3);
	CHECK(rig.engine.Count(mesh::Kind::Commit, 2) >= 1); // the removed one is told as well
	CHECK(rig.glue.CanExecute(stall));
}

TEST_CASE("mesh glue: a view which differs holds the decision back")
{
	GlueRig rig(0);
	const unsigned long stall = mesh::kLag + 3;

	rig.RunTo(stall - 1, {1, 2});
	rig.Packet(1, stall);
	rig.Pass(700, {1});
	REQUIRE(rig.glue.WaitState(rig.now).Removing);
	// the other device holds one packet more of the silent player: no cycle can be named yet
	mesh::Msg view = rig.SameView(1);
	view.have[2] = int32_t(stall);
	rig.glue.OnMeshMessage(1, view, rig.now);
	rig.Pass(100, {1});
	CHECK(rig.engine.quits.empty());
	// it is asked for by relay, arrives, and the views are equal one cycle later
	const mesh::Msg *need = rig.engine.Last(mesh::Kind::Need, 1);
	REQUIRE(need != nullptr);
	CHECK(need->flags == 1);
	CHECK(need->mask == (1u << 2));
	CHECK(need->cycle == int32_t(stall));
	rig.glue.OnPacket(1, 7, stall, false, rig.now);
	rig.Pass(60, {1});
	view = rig.SameView(1);
	rig.glue.OnMeshMessage(1, view, rig.now);
	rig.Pass(20, {1});
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<1>(rig.engine.quits[0]) == stall + 1);
}

TEST_CASE("mesh glue: a player who returns before the bound is not removed")
{
	GlueRig rig(1);
	const unsigned long stall = mesh::kLag + 3;

	rig.RunTo(stall - 1, {0, 2});
	rig.Packet(0, stall);
	rig.Pass(400, {0});
	CHECK(rig.glue.WaitState(rig.now).Player == 7);
	CHECK(rig.glue.CanExecute(stall) == false);

	rig.Packet(2, stall);
	CHECK(rig.glue.WaitState(rig.now).Player == -1);
	CHECK(rig.glue.CanExecute(stall)); // the same cycle the game stopped at
	rig.RunTo(stall + 20, {0, 2});
	rig.Pass(100, {0, 2});
	CHECK(rig.engine.quits.empty());
	CHECK(rig.engine.Count(mesh::Kind::Status, 0) == 0);
	CHECK(rig.glue.Model().RemovedAt(2) == mesh::kNever);
}

TEST_CASE("mesh glue: the absences of a player add up to his allowance")
{
	GlueRig rig(1);
	unsigned long stall = mesh::kLag + 3;

	rig.RunTo(stall - 1, {0, 2});
	// two absences of 400 ms: 800 of the 1000 ms are used
	for (int i = 0; i != 2; ++i) {
		rig.Packet(0, stall);
		rig.Pass(400, {0});
		CHECK(rig.glue.WaitState(rig.now).Removing == false);
		rig.Packet(2, stall);
		REQUIRE(rig.glue.CanExecute(stall));
		rig.glue.OnExecuted(stall, rig.now);
		++stall;
	}
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.Model().AllowanceLeft(2, rig.now) == 200);
	// the third one is bounded by the rest (200 ms), not by the 600 ms of one absence
	rig.Packet(0, stall);
	rig.Pass(150, {0});
	CHECK(rig.glue.WaitState(rig.now).Removing == false);
	CHECK(rig.glue.Model().AllowanceLeft(2, rig.now) == 50);
	rig.Pass(100, {0});
	const CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.Player == 7);
	CHECK(state.Removing);
	CHECK(state.AllowanceMsLeft == 0);
}

// Until the start barrier existed a device never heard was waited for without end ("is not timed
// out"). It is given up now, at the bound of the start.
TEST_CASE("mesh glue: a device never heard is given up at the bound of the start")
{
	GlueRig rig(0, false); // the lowest device: it names the cycle

	rig.glue.OnLoaded(rig.now);
	const int64_t loaded = rig.now;
	rig.glue.OnMeshMessage(1, rig.LoadedBy(0x3), rig.now);
	// slot 2 says nothing at all: no report, no "still loading"
	rig.Pass(300, {1});
	CHECK(rig.glue.Held());
	CHECK(rig.engine.quits.empty());
	CHECK(rig.engine.Count(mesh::Kind::Status, 1) == 0);
	CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.Held);
	CHECK(state.Loaded);
	CHECK(state.NotLoaded == (1u << 7));
	CHECK(state.NoSignal == (1u << 7));
	CHECK(state.Player == 7);
	CHECK(state.Removing == false);
	CHECK(state.MsLeft > 0); // (the model reports the time once a second: no exact number in a test this short)
	CHECK(state.MsLeftOf[7] == state.MsLeft);
	CHECK(state.MsLeftOf[2] == -1);
	CHECK(state.MsLeftOf[4] == -1);
	CHECK(state.RemovedAtStart == 0);

	// the bound of 400 ms: frozen, the devices agree; nothing by the own clock alone
	int64_t suspected = 0;
	for (int t = 0; t < 300 && suspected == 0; t += 10) {
		rig.Pass(10, {1});
		if (rig.glue.Model().IsSuspected(2)) {
			suspected = rig.now;
		}
	}
	REQUIRE(suspected != 0);
	CHECK(suspected - loaded >= 400);
	CHECK(suspected - loaded <= 420);
	CHECK(rig.glue.WaitState(rig.now).Removing);
	CHECK(rig.engine.quits.empty());
	rig.Pass(60, {1});
	rig.glue.OnMeshMessage(1, rig.SameView(1), rig.now);
	rig.glue.Tick(rig.now);
	// out at the first cycle a packet of his is needed
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<0>(rig.engine.quits[0]) == 7);
	CHECK(std::get<1>(rig.engine.quits[0]) == (unsigned long) mesh::kLag + 1);
	CHECK(std::get<2>(rig.engine.quits[0]) == mesh::Reason::Lost);
	REQUIRE(rig.engine.Last(mesh::Kind::Commit, 1) != nullptr);
	CHECK(rig.engine.Last(mesh::Kind::Commit, 1)->player == 2);
	CHECK(rig.engine.Last(mesh::Kind::Commit, 1)->cycle == mesh::kLag + 1);
	CHECK(rig.engine.Last(mesh::Kind::Commit, 2) != nullptr); // he is told as well
	// and the game begins without him
	rig.glue.Tick(rig.now);
	CHECK(rig.glue.Held() == false);
	CHECK(rig.glue.CanExecute(1));
	state = rig.glue.WaitState(rig.now);
	CHECK(state.Held == false);
	CHECK(state.NotLoaded == 0);
	CHECK(state.RemovedAtStart == (1u << 7));
	CHECK(state.Player == -1);
	rig.RunTo(mesh::kLag, {});
	CHECK(rig.glue.CanExecute(mesh::kLag + 1) == false); // the packet of slot 1 is needed, his is not
	rig.Packet(1, mesh::kLag + 1);
	CHECK(rig.glue.CanExecute(mesh::kLag + 1));
}

TEST_CASE("mesh glue: the quit of a player who leaves is his own packet")
{
	GlueRig rig(1);
	const unsigned long last = mesh::kLag + 3;

	rig.RunTo(last - 1, {0, 2});
	rig.Packet(2, last, true);
	// the engine is told, and finds his own quit in the queue
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<0>(rig.engine.quits[0]) == 7);
	CHECK(std::get<1>(rig.engine.quits[0]) == last);
	CHECK(std::get<2>(rig.engine.quits[0]) == mesh::Reason::Left);
	CHECK(rig.glue.Accepts(2)); // his earlier packets are still taken
	CHECK(rig.glue.CanExecute(last) == false);
	rig.Packet(0, last);
	CHECK(rig.glue.CanExecute(last));
	rig.RunTo(last + 5, {0});
	rig.Pass(1000, {0});
	CHECK(rig.glue.WaitState(rig.now).Player == -1);
	CHECK(rig.engine.quits.size() == 1);
}

TEST_CASE("mesh glue: commits which are not for this device")
{
	GlueRig rig(1);
	const unsigned long cycle = mesh::kLag + 5;

	rig.RunTo(mesh::kLag, {});
	// from a device which has no host slot
	rig.glue.OnMeshMessage(3, rig.Commit(2, cycle, 0x3), rig.now);
	rig.glue.OnMeshMessage(-1, rig.Commit(2, cycle, 0x3), rig.now);
	// from this device's own slot
	rig.glue.OnMeshMessage(1, rig.Commit(2, cycle, 0x3), rig.now);
	// decided by a group this device is not part of
	rig.glue.OnMeshMessage(0, rig.Commit(2, cycle, 0x1), rig.now);
	// about a player who does not exist
	rig.glue.OnMeshMessage(0, rig.Commit(5, cycle, 0x3), rig.now);
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.WaitState(rig.now).SelfOut == false);

	// a commit about this device: it is out, nothing runs any more, nothing is written
	rig.glue.OnMeshMessage(0, rig.Commit(1, cycle, 0x5), rig.now);
	const CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.SelfOut);
	CHECK(state.SelfOutReason == int(mesh::Reason::ConnectionLost));
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.CanExecute(mesh::kLag + 1) == false);
}

TEST_CASE("mesh glue: a device which hears nobody in a game of three is out itself")
{
	GlueRig rig(1);

	rig.RunTo(mesh::kLag, {});
	rig.Packet(0, mesh::kLag);
	rig.Packet(2, mesh::kLag);
	rig.Pass(1000, {});
	const CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.SelfOut);
	CHECK(state.SelfOutReason == int(mesh::Reason::Isolated));
	CHECK(rig.engine.quits.empty());
}

TEST_CASE("mesh glue: leaving")
{
	GlueRig rig(1);

	rig.RunTo(mesh::kLag + 2, {0, 2});
	rig.glue.OnQuit(rig.now);
	// the quit packet is the engine's to send; leaving is no lost connection
	CHECK(rig.engine.packets.empty());
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.WaitState(rig.now).SelfOut == false);
	CHECK(rig.glue.CanExecute(mesh::kLag + 3) == false);
	// requests are still answered while the process serves the socket
	mesh::Msg need;
	need.kind = mesh::Kind::Need;
	need.cycle = mesh::kLag + 2;
	need.mask = uint16_t(1u << 1);
	rig.glue.OnMeshMessage(0, need, rig.now);
	CHECK(rig.engine.packets.empty() == false);
}

//
// The start barrier (B.4) and the leave (B.6 step 2).
//

TEST_CASE("mesh glue: the game is held until every device has loaded")
{
	GlueRig rig(1, false);

	CHECK(rig.glue.Held());
	CHECK(rig.glue.Loaded() == false);
	CHECK(rig.glue.CanExecute(1) == false);
	// while this device loads it tells the others so, four or five times in 200 ms, and nothing else
	rig.Pass(200, {});
	CHECK(rig.engine.Count(mesh::Kind::Loading, 0) >= 4);
	CHECK(rig.engine.Count(mesh::Kind::Loading, 0) <= 5);
	CHECK(rig.engine.Count(mesh::Kind::Loading, 2) == rig.engine.Count(mesh::Kind::Loading, 0));
	CHECK(rig.engine.meshes.size() == size_t(2 * rig.engine.Count(mesh::Kind::Loading, 0)));
	CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.Held);
	CHECK(state.Loaded == false);
	CHECK(state.NotLoaded == ((1u << 4) | (1u << 7)));
	CHECK(state.NoSignal == 0); // nobody is listened to before the own load is done
	CHECK(state.Player == -1);  // and no bound runs
	CHECK(state.MsLeftOf[4] == -1);
	// however long the own load takes, nobody is given up meanwhile
	rig.Pass(3000, {});
	CHECK(rig.engine.quits.empty());
	CHECK(rig.engine.Count(mesh::Kind::Status, 0) == 0);
	CHECK(rig.glue.Held());

	// the own load ends: the report goes out instead, with the devices known as loaded
	rig.engine.meshes.clear();
	rig.glue.OnLoaded(rig.now);
	CHECK(rig.engine.meshes.empty()); // in the next tick
	rig.Pass(100, {0, 2});
	CHECK(rig.engine.Count(mesh::Kind::Loading, 0) == 0);
	CHECK(rig.engine.Count(mesh::Kind::Loaded, 0) >= 2);
	CHECK(rig.engine.Count(mesh::Kind::Loaded, 2) >= 2);
	REQUIRE(rig.engine.Last(mesh::Kind::Loaded, 0) != nullptr);
	CHECK(rig.engine.Last(mesh::Kind::Loaded, 0)->mask == 0x2);
	CHECK(rig.glue.Held());
	state = rig.glue.WaitState(rig.now);
	CHECK(state.Loaded);
	CHECK(state.NotLoaded == ((1u << 4) | (1u << 7)));
	CHECK(state.NoSignal == 0); // both still say that they load
	CHECK(state.Player != -1);
	CHECK(state.MsLeftOf[4] > 1000); // the long bound
	CHECK(state.MsLeftOf[4] <= 1200);
	CHECK(state.MsLeftOf[7] > 1000);

	// one of the two reports
	rig.glue.OnMeshMessage(0, rig.LoadedBy(0x1), rig.now);
	rig.Pass(60, {0, 2});
	CHECK(rig.glue.Held());
	CHECK(rig.glue.CanExecute(1) == false);
	state = rig.glue.WaitState(rig.now);
	CHECK(state.NotLoaded == (1u << 7));
	CHECK(state.Player == 7);
	CHECK(rig.engine.Last(mesh::Kind::Loaded, 2)->mask == 0x3);
	// the other one: the game begins
	rig.glue.OnMeshMessage(2, rig.LoadedBy(0x4), rig.now);
	rig.glue.Tick(rig.now);
	CHECK(rig.glue.Held() == false);
	CHECK(rig.glue.CanExecute(1));
	state = rig.glue.WaitState(rig.now);
	CHECK(state.Held == false);
	CHECK(state.NotLoaded == 0);
	CHECK(state.RemovedAtStart == 0);
	CHECK(state.Player == -1);
	CHECK(rig.engine.quits.empty());

	// a device which has not got this device's report says its own again and is answered; one which has, is not
	rig.engine.meshes.clear();
	rig.glue.OnMeshMessage(2, rig.LoadedBy(0x4), rig.now);
	CHECK(rig.engine.Count(mesh::Kind::Loaded, 2) == 1);
	CHECK(rig.engine.Last(mesh::Kind::Loaded, 2)->mask == 0x7);
	rig.glue.OnMeshMessage(0, rig.LoadedBy(0x7), rig.now);
	CHECK(rig.engine.Count(mesh::Kind::Loaded, 0) == 0);
	// after the start the report is not repeated by itself
	rig.engine.meshes.clear();
	rig.Pass(200, {0, 2});
	CHECK(rig.engine.Count(mesh::Kind::Loaded, 0) == 0);
	CHECK(rig.engine.Count(mesh::Kind::Loading, 0) == 0);
}

TEST_CASE("mesh glue: a report of a load comes second hand, and the first packet of a device is one")
{
	GlueRig rig(1, false);

	rig.glue.OnLoaded(rig.now);
	// slot 0 has loaded and holds the report of slot 2, which did not reach us
	rig.glue.OnMeshMessage(0, rig.LoadedBy(0x5), rig.now);
	rig.glue.Tick(rig.now);
	CHECK(rig.glue.Held() == false);

	GlueRig other(1, false);
	other.glue.OnLoaded(other.now);
	other.glue.OnMeshMessage(0, other.LoadedBy(0x1), other.now);
	other.glue.Tick(other.now);
	CHECK(other.glue.Held());
	// slot 2 runs already: its packet of its first cycle says that it has loaded
	other.Packet(2, mesh::kLag + 1);
	other.glue.Tick(other.now);
	CHECK(other.glue.Held() == false);
	CHECK(other.glue.WaitState(other.now).RemovedAtStart == 0);
}

TEST_CASE("mesh glue: a device which loads without end is given up at the long bound")
{
	GlueRig rig(0, false);

	rig.glue.OnLoaded(rig.now);
	const int64_t loaded = rig.now;
	rig.glue.OnMeshMessage(1, rig.LoadedBy(0x3), rig.now);
	// slot 2 is alive and says so, far beyond the short bound
	rig.Pass(1100, {1, 2});
	CHECK(rig.glue.Held());
	CHECK(rig.glue.Model().IsSuspected(2) == false);
	CHECK(rig.engine.quits.empty());
	CHECK(rig.engine.Count(mesh::Kind::Status, 1) == 0);
	CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.NotLoaded == (1u << 7));
	CHECK(state.NoSignal == 0);
	CHECK(state.Player == 7);
	CHECK(state.MsLeft <= 200);
	// 1200 ms after the own end of the load it is over
	int64_t suspected = 0;
	for (int t = 0; t < 300 && suspected == 0; t += 10) {
		rig.Pass(10, {1, 2});
		if (rig.glue.Model().IsSuspected(2)) {
			suspected = rig.now;
		}
	}
	REQUIRE(suspected != 0);
	CHECK(suspected - loaded >= 1200);
	CHECK(suspected - loaded <= 1220);
	rig.Pass(60, {1, 2}); // what he still says is not listened to
	rig.glue.OnMeshMessage(1, rig.SameView(1), rig.now);
	rig.glue.Tick(rig.now);
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<0>(rig.engine.quits[0]) == 7);
	CHECK(std::get<1>(rig.engine.quits[0]) == (unsigned long) mesh::kLag + 1);
	rig.glue.Tick(rig.now);
	CHECK(rig.glue.Held() == false);
	CHECK(rig.glue.WaitState(rig.now).RemovedAtStart == (1u << 7));
}

TEST_CASE("mesh glue: who leaves or is cut off while the game is held")
{
	GlueRig rig(1, false);

	rig.glue.OnLoaded(rig.now);
	rig.glue.OnMeshMessage(0, rig.LoadedBy(0x1), rig.now);
	// slot 2 leaves through its menu before the start: its quit is its packet of the first free cycle
	rig.Packet(2, mesh::kLag + 1, true);
	REQUIRE(rig.engine.quits.size() == 1);
	CHECK(std::get<0>(rig.engine.quits[0]) == 7);
	CHECK(std::get<1>(rig.engine.quits[0]) == (unsigned long) mesh::kLag + 1);
	CHECK(std::get<2>(rig.engine.quits[0]) == mesh::Reason::Left);
	rig.glue.Tick(rig.now);
	CHECK(rig.glue.Held() == false); // nobody waits for him
	CHECK(rig.glue.WaitState(rig.now).RemovedAtStart == (1u << 7));

	// the others started without this device: it is told, stays held and is out
	GlueRig late(1, false);
	late.glue.OnLoaded(late.now);
	late.glue.OnMeshMessage(0, late.Commit(1, mesh::kLag + 1, 0x5), late.now);
	late.glue.OnMeshMessage(0, late.LoadedBy(0x5), late.now);
	late.glue.OnMeshMessage(2, late.LoadedBy(0x5), late.now);
	late.Pass(100, {0, 2});
	const CNetworkWaitState state = late.glue.WaitState(late.now);
	CHECK(state.SelfOut);
	CHECK(state.SelfOutReason == int(mesh::Reason::ConnectionLost));
	CHECK(state.Held);
	CHECK(late.glue.CanExecute(1) == false);
	CHECK(late.engine.quits.empty());
}

TEST_CASE("mesh glue: who has left stays until nobody asks any more, and not beyond the cap")
{
	mesh::Msg need;
	need.kind = mesh::Kind::Need;
	need.cycle = mesh::kLag + 3;
	need.mask = uint16_t(1u << 1);

	// nobody asks: 100 ms
	GlueRig quiet(1);
	quiet.RunTo(mesh::kLag + 2, {0, 2});
	CHECK(quiet.glue.Lingering() == false);
	quiet.glue.OnQuit(quiet.now);
	CHECK(quiet.glue.Lingering());
	quiet.Pass(90, {0, 2});
	CHECK(quiet.glue.Lingering());
	quiet.Pass(20, {0, 2});
	CHECK(quiet.glue.Lingering() == false);
	CHECK(quiet.glue.WaitState(quiet.now).SelfOut == false);

	// one request after 80 ms: answered, and 100 ms from there
	GlueRig asked(1);
	asked.RunTo(mesh::kLag + 2, {0, 2});
	asked.glue.OnQuit(asked.now);
	asked.Pass(80, {});
	asked.glue.OnMeshMessage(0, need, asked.now);
	REQUIRE(asked.engine.packets.empty() == false);
	CHECK(std::get<0>(asked.engine.packets[0]) == 0);
	CHECK(std::get<1>(asked.engine.packets[0]) == 2);
	CHECK(std::get<2>(asked.engine.packets[0]) == (unsigned long) mesh::kLag + 3);
	asked.Pass(90, {});
	CHECK(asked.glue.Lingering());
	asked.Pass(20, {});
	CHECK(asked.glue.Lingering() == false);

	// somebody asks without end: 400 ms
	GlueRig capped(1);
	capped.RunTo(mesh::kLag + 2, {0, 2});
	capped.glue.OnQuit(capped.now);
	for (int t = 0; t < 390; t += 10) {
		if (t % 50 == 0) {
			capped.glue.OnMeshMessage(2, need, capped.now);
		}
		capped.Pass(10, {});
	}
	CHECK(capped.glue.Lingering());
	capped.Pass(20, {});
	CHECK(capped.glue.Lingering() == false);
	// gone: nothing is answered any more
	capped.engine.packets.clear();
	capped.glue.OnMeshMessage(2, need, capped.now);
	CHECK(capped.engine.packets.empty());
}

TEST_CASE("mesh glue: a pause for everybody: no cycle runs, nobody heard is waited for, anyone ends it")
{
	GlueRig rig(0);
	rig.RunTo(5, {1, 2});
	// the pause command of the player in host slot 1 ran in cycle 5
	rig.glue.OnPause(SlotPlayer[1], rig.now);
	REQUIRE(rig.engine.pauses.size() == 1);
	CHECK(rig.engine.pauses[0] == std::make_pair(true, SlotPlayer[1]));
	CHECK(rig.glue.Paused());
	CHECK(rig.glue.PausedBy() == SlotPlayer[1]);
	rig.Packet(1, 6);
	rig.Packet(2, 6);
	CHECK_FALSE(rig.glue.CanExecute(6));
	// a player who has no slot pauses nothing; a second command neither
	rig.glue.OnPause(9, rig.now);
	rig.glue.OnPause(SlotPlayer[2], rig.now);
	CHECK(rig.engine.pauses.size() == 1);

	// far beyond the bound of an absence (600 ms): the others stand too and say so
	mesh::Msg stands;
	stands.kind = mesh::Kind::Pause;
	stands.player = 1;
	stands.flags = 1;
	stands.cycle = 5;
	for (int t = 0; t < 2000; t += 50) {
		rig.glue.OnMeshMessage(1, stands, rig.now);
		rig.glue.OnMeshMessage(2, stands, rig.now);
		rig.Pass(50, {});
	}
	CNetworkWaitState state = rig.glue.WaitState(rig.now);
	CHECK(state.Paused);
	CHECK(state.PausedBy == SlotPlayer[1]);
	CHECK(state.Player == -1);
	CHECK(rig.engine.quits.empty());
	CHECK(rig.glue.Model().AllowanceLeft(1, rig.now) == 1000);
	CHECK(rig.glue.Model().AllowanceLeft(2, rig.now) == 1000);
	// this device says four times a second (here: every 50 ms) that it stands
	CHECK(rig.engine.Count(mesh::Kind::Pause, 1) >= 30);
	const mesh::Msg *own = rig.engine.Last(mesh::Kind::Pause, 2);
	REQUIRE(own != nullptr);
	CHECK(own->flags == 1);
	CHECK(own->cycle == 5);
	CHECK(own->player == 1);

	// the device in slot 2 goes silent: it is waited for as in a running game
	for (int t = 0; t < 200; t += 50) {
		rig.glue.OnMeshMessage(1, stands, rig.now);
		rig.Pass(50, {});
	}
	state = rig.glue.WaitState(rig.now);
	CHECK(state.Paused);
	CHECK(state.Player == SlotPlayer[2]);
	rig.glue.OnMeshMessage(2, stands, rig.now);

	// the player in slot 2 lets the game go on
	mesh::Msg over;
	over.kind = mesh::Kind::Pause;
	over.player = 2;
	over.cycle = 5;
	rig.glue.OnMeshMessage(2, over, rig.now);
	REQUIRE(rig.engine.pauses.size() == 2);
	CHECK(rig.engine.pauses[1] == std::make_pair(false, SlotPlayer[2]));
	CHECK_FALSE(rig.glue.Paused());
	CHECK(rig.glue.PausedBy() == -1);
	CHECK(rig.glue.CanExecute(6));
	CHECK_FALSE(rig.glue.WaitState(rig.now).Paused);
	// the device in slot 1 has not heard of it: its heartbeat is answered
	rig.engine.meshes.clear();
	rig.glue.OnMeshMessage(1, stands, rig.now);
	const mesh::Msg *answer = rig.engine.Last(mesh::Kind::Pause, 1);
	REQUIRE(answer != nullptr);
	CHECK(answer->flags == 0);
	CHECK(answer->cycle == 5);
	CHECK(answer->player == 2);
}

TEST_CASE("mesh glue: a chat line goes to every device with its text until each has answered")
{
	GlueRig rig(1);
	rig.RunTo(5, {0, 2});
	rig.glue.OnPause(SlotPlayer[0], rig.now); // a standing game: the line travels all the same
	REQUIRE(rig.glue.Paused());
	REQUIRE(rig.glue.OnChat("go on?", rig.now));
	REQUIRE(rig.engine.chatsSent.size() == 2);
	CHECK(rig.engine.chatsSent[0] == std::make_tuple(0, uint32_t(1), std::string("go on?")));
	CHECK(rig.engine.chatsSent[1] == std::make_tuple(2, uint32_t(1), std::string("go on?")));
	CHECK(rig.engine.chatsShown.empty()); // the own line is shown by who said it
	CHECK(rig.engine.Count(mesh::Kind::Chat, 0) == 0); // never as a message without its text

	// the device in host slot 0 answers; the one in slot 2 gets the line again, with the text
	mesh::Msg answer;
	answer.kind = mesh::Kind::ChatAck;
	answer.data = 1;
	rig.glue.OnMeshMessage(0, answer, rig.now);
	rig.engine.chatsSent.clear();
	rig.now += 300;
	rig.glue.Tick(rig.now);
	REQUIRE(rig.engine.chatsSent.size() == 1);
	CHECK(rig.engine.chatsSent[0] == std::make_tuple(2, uint32_t(1), std::string("go on?")));
	rig.glue.OnMeshMessage(2, answer, rig.now);
	rig.engine.chatsSent.clear();
	rig.now += 300;
	rig.glue.Tick(rig.now);
	CHECK(rig.engine.chatsSent.empty());
	CHECK(rig.glue.Paused());
}

TEST_CASE("mesh glue: a chat line of another player is shown once and always answered")
{
	GlueRig rig(0);
	rig.RunTo(5, {1, 2});
	rig.glue.OnChatMessage(2, 1, "ready", rig.now);
	REQUIRE(rig.engine.chatsShown.size() == 1);
	CHECK(rig.engine.chatsShown[0] == std::make_pair(SlotPlayer[2], std::string("ready")));
	CHECK(rig.engine.Count(mesh::Kind::ChatAck, 2) == 1);
	// the same line again (its answer was lost): answered, not shown
	rig.glue.OnChatMessage(2, 1, "ready", rig.now);
	CHECK(rig.engine.chatsShown.size() == 1);
	CHECK(rig.engine.Count(mesh::Kind::ChatAck, 2) == 2);
	REQUIRE(rig.engine.Last(mesh::Kind::ChatAck, 2) != nullptr);
	CHECK(rig.engine.Last(mesh::Kind::ChatAck, 2)->data == 1);
	// a line from no device of the game, and the bare message of the model, are nothing
	rig.glue.OnChatMessage(5, 1, "who", rig.now);
	mesh::Msg bare;
	bare.kind = mesh::Kind::Chat;
	bare.player = 1;
	bare.data = 9;
	rig.glue.OnMeshMessage(1, bare, rig.now);
	CHECK(rig.engine.chatsShown.size() == 1);
	CHECK(rig.engine.Count(mesh::Kind::ChatAck, 1) == 0);
	// a second line of the same player
	rig.glue.OnChatMessage(2, 2, "now", rig.now);
	REQUIRE(rig.engine.chatsShown.size() == 2);
	CHECK(rig.engine.chatsShown[1].second == "now");
}

TEST_CASE("mesh glue: the local player lets a paused game go on")
{
	GlueRig rig(1);
	rig.RunTo(5, {0, 2});
	rig.glue.OnResume(rig.now); // nothing is paused: nothing is said
	CHECK(rig.engine.Count(mesh::Kind::Pause, 0) == 0);
	rig.glue.OnPause(SlotPlayer[0], rig.now);
	REQUIRE(rig.glue.Paused());
	rig.engine.meshes.clear();
	rig.glue.OnResume(rig.now);
	CHECK_FALSE(rig.glue.Paused());
	REQUIRE(rig.engine.pauses.size() == 2);
	CHECK(rig.engine.pauses[1] == std::make_pair(false, SlotPlayer[1]));
	for (int slot : {0, 2}) {
		const mesh::Msg *over = rig.engine.Last(mesh::Kind::Pause, slot);
		REQUIRE(over != nullptr);
		CHECK(over->flags == 0);
		CHECK(over->cycle == 5);
		CHECK(over->player == 1);
	}
}
