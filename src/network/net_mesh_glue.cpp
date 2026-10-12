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
/**@name net_mesh_glue.cpp - The mesh model beside the command queue of the engine. */
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


//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "net_mesh_glue.h"

#include <algorithm>

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

/// A waiting event of the model comes once a second; without a new one it is over after this time
static constexpr int64_t WaitEventFreshMs = 1500;

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/// Bit of a player number in the masks of CNetworkWaitState, 0 if it has no place there
static unsigned int PlayerBit(int player)
{
	return player >= 0 && player < int(CNetworkWaitState().MsLeftOf.size()) ? 1u << player : 0;
}

/**
**  Start the model for a game which loads now. The game is held until the model
**  lets it go (B.4).
**
**  @param engine       The engine side.
**  @param slotPlayers  Player number of every host slot, without a gap.
**  @param ownSlot      Host slot of this device.
**  @param params       Time bounds of the model.
**  @param now          Time in ms.
*/
CMeshGlue::CMeshGlue(CMeshGlueEngine &engine, const std::vector<int> &slotPlayers, int ownSlot,
                     const mesh::Params &params, int64_t now) :
	engine(engine),
	slotPlayers(slotPlayers),
	ownSlot(ownSlot),
	model(int(slotPlayers.size()), ownSlot, params, now)
{
	// The engine counts its cycles from 1 and makes the packet for 1 + lag in cycle 1;
	// for the model that is the packet made after cycle 0.
	model.OnExecuted(0);
	model.out.clear();
	model.events.clear();
}

int CMeshGlue::PlayerOfSlot(int hostSlot) const
{
	if (hostSlot < 0 || hostSlot >= int(slotPlayers.size())) {
		return -1;
	}
	return slotPlayers[hostSlot];
}

int CMeshGlue::SlotOfPlayer(int player) const
{
	const auto it = std::find(slotPlayers.begin(), slotPlayers.end(), player);

	return it == slotPlayers.end() ? -1 : int(it - slotPlayers.begin());
}

/**
**  Packets from the address of a host slot are taken: the player is not frozen by a
**  removal round and not removed as lost. A player who left still supplies his last packets.
*/
bool CMeshGlue::Accepts(int hostSlot) const
{
	if (PlayerOfSlot(hostSlot) == -1 || hostSlot == ownSlot || model.IsSuspected(hostSlot)) {
		return false;
	}
	return model.RemovedAt(hostSlot) == mesh::kNever || model.RemovedWhy(hostSlot) == mesh::Reason::Left;
}

/**
**  Something arrived from the address of a host slot, and the model has it: he is
**  not waited for any more. One who only says that he still loads is.
*/
void CMeshGlue::Heard(int hostSlot)
{
	if (PlayerOfSlot(hostSlot) == -1) {
		return;
	}
	if (!model.IsSuspected(hostSlot) && (model.LoadedMask() & (1u << hostSlot))) {
		waitAt[hostSlot] = 0;
	}
}

/**
**  The own load of the game is done. The model tells the others in its ticks, and
**  its bounds for the ones which have not loaded run from here.
*/
void CMeshGlue::OnLoaded(int64_t now)
{
	if (model.IsLoaded()) {
		return;
	}
	model.OnLoaded(now);
	Drain(false, now);
}

/**
**  The queue of the engine holds the packet of a player for a cycle.
**
**  @param hostSlot  Device it came from; another one than the player's is a relay.
**  @param player    Player number of the packet.
**  @param cycle     Cycle of the packet.
**  @param quit      The packet is the player's quit.
**  @param now       Time in ms.
*/
void CMeshGlue::OnPacket(int hostSlot, int player, unsigned long cycle, bool quit, int64_t now)
{
	const int slot = SlotOfPlayer(player);

	if (slot == -1 || PlayerOfSlot(hostSlot) == -1 || cycle > (unsigned long) mesh::kNever - 1) {
		return;
	}
	mesh::Msg message;
	message.kind = mesh::Kind::Cmd;
	message.player = uint8_t(slot);
	message.cycle = int32_t(cycle);
	message.flags = quit ? 1 : 0;
	model.OnMessage(hostSlot, message, now);
	Heard(hostSlot);
	Drain(false, now);
}

void CMeshGlue::OnHeard(int hostSlot, int64_t now)
{
	if (PlayerOfSlot(hostSlot) == -1) {
		return;
	}
	// The model has no message without content; this one only counts as a sign of life.
	mesh::Msg message;
	message.kind = mesh::Kind::Loading;
	model.OnMessage(hostSlot, message, now);
	Heard(hostSlot);
	Drain(false, now);
}

void CMeshGlue::OnMeshMessage(int hostSlot, const mesh::Msg &message, int64_t now)
{
	// A command of the model stands for a command packet, which has its own way in; so has
	// a chat line, which comes with its text.
	if (PlayerOfSlot(hostSlot) == -1 || message.kind == mesh::Kind::Cmd || message.kind == mesh::Kind::Chat) {
		return;
	}
	model.OnMessage(hostSlot, message, now);
	Heard(hostSlot);
	Drain(false, now);
}

void CMeshGlue::OnExecuted(unsigned long cycle, int64_t now)
{
	if (model.IsOut() || int32_t(cycle) != model.Executed() + 1) {
		return;
	}
	model.OnExecuted(int32_t(cycle));
	Drain(true, now);
}

void CMeshGlue::OnQuit(int64_t now)
{
	model.Quit(now);
	Drain(true, now);
}

/**
**  The pause command of a player ran in the cycle executed last: from here no cycle
**  runs on any device until one of them lets the game go on.
*/
void CMeshGlue::OnPause(int player, int64_t now)
{
	const int slot = SlotOfPlayer(player);

	if (slot == -1) {
		return;
	}
	model.Pause(slot, now);
	Drain(false, now);
}

void CMeshGlue::OnResume(int64_t now)
{
	model.Resume(now);
	Drain(false, now);
}

/**
**  The local player says a chat line. The model sends it to every device and repeats
**  it until each has answered; the text is kept for that.
**
**  @return  false if the model takes no line now.
*/
bool CMeshGlue::OnChat(const std::string &text, int64_t now)
{
	const uint32_t number = model.Chat(now);

	if (number == 0) {
		return false;
	}
	// The model repeats only the lines of its window; twice that many are kept.
	if (ownChat.size() == 2 * mesh::kChatWindow) {
		ownChat.pop_front();
	}
	ownChat.emplace_back(number, text);
	Drain(false, now);
	return true;
}

/**
**  A chat line arrived from the address of a host slot. The model answers it and
**  tells whether it is new; a new one is shown.
*/
void CMeshGlue::OnChatMessage(int hostSlot, uint32_t number, const std::string &text, int64_t now)
{
	if (PlayerOfSlot(hostSlot) == -1) {
		return;
	}
	mesh::Msg message;
	message.kind = mesh::Kind::Chat;
	message.player = uint8_t(hostSlot);
	message.data = number;
	model.OnMessage(hostSlot, message, now);
	Heard(hostSlot);
	chatArrived = &text;
	Drain(false, now);
	chatArrived = nullptr;
}

void CMeshGlue::Tick(int64_t now)
{
	model.Tick(now);
	Drain(false, now);
}

bool CMeshGlue::CanExecute(unsigned long cycle) const
{
	return cycle <= (unsigned long) mesh::kNever - 1 && model.CanExecute(int32_t(cycle));
}

/**
**  Who is waited for: the player with the least time left. Nobody while the next cycle
**  may run (the packets of a player who is not heard himself can come by another device).
**  A paused game waits for nobody who is heard; a silent player is waited for as ever.
**  While the game is held: also who has not reported his load, and who of them is silent.
*/
CNetworkWaitState CMeshGlue::WaitState(int64_t now) const
{
	CNetworkWaitState state;

	state.SelfOut = selfOut;
	state.SelfOutReason = int(selfOutReason);
	state.Held = Held();
	state.Loaded = model.IsLoaded();
	state.RemovedAtStart = removedAtStart;
	state.Paused = Paused();
	state.PausedBy = PausedBy();
	if (model.CanExecute(model.Executed() + 1)) {
		return state;
	}
	for (int slot = 0; slot != int(slotPlayers.size()); ++slot) {
		if (slot == ownSlot || model.RemovedAt(slot) != mesh::kNever) {
			continue;
		}
		const unsigned int playerBit = PlayerBit(slotPlayers[slot]);
		if (state.Held && !(model.LoadedMask() & (1u << slot))) {
			state.NotLoaded |= playerBit;
			// The own clocks run from the own end of the load; before it nobody is listened to.
			if (model.IsLoaded() && model.IsQuiet(slot, now)) {
				state.NoSignal |= playerBit;
			}
		}
		const bool removing = model.IsSuspected(slot);
		int left = 0;
		if (!removing) {
			if (waitAt[slot] == 0 || now - waitAt[slot] >= WaitEventFreshMs) {
				continue;
			}
			left = int(std::max<int64_t>(0, waitLeft[slot] - (now - waitAt[slot])));
		}
		if (playerBit) {
			state.MsLeftOf[slotPlayers[slot]] = left;
		}
		if (state.Player == -1 || left < state.MsLeft) {
			state.Player = slotPlayers[slot];
			state.MsLeft = left;
			state.AllowanceMsLeft = int(model.AllowanceLeft(slot, now));
			state.Removing = removing;
		}
	}
	return state;
}

/**
**  Hand the messages and the events of the model to the engine.
**
**  @param ownPackets  The commands in the queue are the fresh own packets, which the
**                     engine sends itself; otherwise they are answers to a request.
**  @param now         Time in ms.
*/
void CMeshGlue::Drain(bool ownPackets, int64_t now)
{
	std::vector<mesh::Out> out;
	std::vector<mesh::Event> events;

	out.swap(model.out);
	events.swap(model.events);
	for (const mesh::Out &o : out) {
		if (o.msg.kind == mesh::Kind::Chat) {
			for (const auto &line : ownChat) {
				if (line.first == o.msg.data) {
					engine.SendChat(o.to, line.first, line.second);
					break;
				}
			}
		} else if (o.msg.kind != mesh::Kind::Cmd) {
			engine.SendMesh(o.to, o.msg);
		} else if (!ownPackets) {
			engine.SendPacket(o.to, PlayerOfSlot(o.msg.player), o.msg.cycle);
		}
	}
	for (const mesh::Event &event : events) {
		switch (event.type) {
			case mesh::Event::Started:
				break; // Held() is over
			case mesh::Event::Waiting:
				waitAt[event.player] = std::max<int64_t>(now, 1);
				waitLeft[event.player] = event.msLeft;
				break;
			case mesh::Event::PlayerRemoved:
				waitAt[event.player] = 0;
				if (event.player != ownSlot) {
					if (Held()) {
						removedAtStart |= PlayerBit(PlayerOfSlot(event.player));
					}
					engine.InstallQuit(PlayerOfSlot(event.player), event.cycle, event.reason);
				}
				break;
			case mesh::Event::SelfOut:
				if (event.reason != mesh::Reason::Left) {
					selfOut = true;
					selfOutReason = event.reason;
				}
				break;
			case mesh::Event::Paused:
				engine.SetPaused(true, PlayerOfSlot(event.player));
				break;
			case mesh::Event::Resumed:
				engine.SetPaused(false, PlayerOfSlot(event.player));
				break;
			case mesh::Event::ChatLine:
				if (chatArrived) {
					engine.ShowChat(PlayerOfSlot(event.player), *chatArrived);
				}
				break;
		}
	}
}

//@}
