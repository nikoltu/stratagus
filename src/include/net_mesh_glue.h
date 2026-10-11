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
/**@name net_mesh_glue.h - The mesh model beside the command queue of the engine. */
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

#ifndef __NET_MESH_GLUE_H__
#define __NET_MESH_GLUE_H__

//@{

/*----------------------------------------------------------------------------
--  Documentation
----------------------------------------------------------------------------*/

/**
**  @class CMeshGlue net_mesh_glue.h
**
**  The model of a mesh game (mesh::Mesh, net_mesh.h) decides who is asked for a
**  packet, who is waited for, who is removed at which cycle and whether a cycle
**  may run. The command packets themselves stay in the queue of the engine
**  (network.cpp). This class translates between the two:
**
**  engine -> model: the own load is done (OnLoaded), a packet of a player for a
**  cycle is held (OnPacket), a device was heard (OnHeard), a mesh message arrived
**  (OnMeshMessage), a cycle was executed (OnExecuted), the local player leaves
**  (OnQuit), the pause command of a player ran (OnPause), the local player lets a
**  paused game go on (OnResume), time passes (Tick).
**
**  model -> engine (CMeshGlueEngine): send a mesh message, send a held packet to
**  one device (the answer to a request), write the quit of a player into a cycle,
**  the game is paused or goes on (SetPaused);
**  and the answers CanExecute, Held, Lingering and WaitState.
**
**  The start: the game is held until every device has reported its load (the
**  barrier of the model). Before OnLoaded a Tick only tells the others that this
**  device still loads; from OnLoaded on the model sends its report, waits for the
**  others up to its bounds and removes the ones which stay silent. Held ends with
**  the event Started of the model.
**
**  The model's command (mesh::Kind::Cmd) never travels: it stands for a command
**  packet of the engine. Its content is of no interest to the model.
**
**  Players: the model and the wire count in host slots, the engine in player
**  numbers. PlayerOfSlot and SlotOfPlayer are the only translation.
**
**  Everything is called from the thread of the game loop.
*/

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "net_mesh.h"
#include "network.h"

#include <cstdint>
#include <vector>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

/**
**  What the glue needs from the engine.
*/
class CMeshGlueEngine
{
public:
	virtual ~CMeshGlueEngine() {}

	/// Send a mesh message to the device in a host slot
	virtual void SendMesh(int hostSlot, const mesh::Msg &message) = 0;
	/// Send the held packet of a player for a cycle to the device in a host slot, nothing if it is not held
	virtual void SendPacket(int hostSlot, int player, unsigned long cycle) = 0;
	/// Write the quit of a player into his place of a cycle (which is not executed yet)
	virtual void InstallQuit(int player, unsigned long cycle, mesh::Reason reason) = 0;
	/// The game is paused for everybody by a player, or goes on by a player
	virtual void SetPaused(bool paused, int player) = 0;
};

class CMeshGlue
{
public:
	/// A game which loads now: held, cycle 0 is done, the packets of the cycles 0..lag are held
	CMeshGlue(CMeshGlueEngine &engine, const std::vector<int> &slotPlayers, int ownSlot,
	          const mesh::Params &params, int64_t now);

	/// Player number of a host slot, -1 if there is none
	int PlayerOfSlot(int hostSlot) const;
	/// Host slot of a player number, -1 if he has none
	int SlotOfPlayer(int player) const;

	/// Packets from the address of that host slot are taken (he is not frozen, not removed as lost)
	bool Accepts(int hostSlot) const;
	/// The own load of the game is done
	void OnLoaded(int64_t now);
	/// The queue holds the packet of player for cycle; it came from the device in hostSlot
	void OnPacket(int hostSlot, int player, unsigned long cycle, bool quit, int64_t now);
	/// Something else arrived from the address of that host slot
	void OnHeard(int hostSlot, int64_t now);
	/// A mesh message arrived from the address of that host slot
	void OnMeshMessage(int hostSlot, const mesh::Msg &message, int64_t now);
	/// The cycle was executed and the own packet for cycle + lag + 1 is the next one made
	void OnExecuted(unsigned long cycle, int64_t now);
	/// The local player leaves; his quit packet is the next own packet
	void OnQuit(int64_t now);
	/// The pause command of a player ran in the cycle executed last
	void OnPause(int player, int64_t now);
	/// The local player lets a paused game go on
	void OnResume(int64_t now);
	/// Time passes: the start, silence, requests, the removal round
	void Tick(int64_t now);

	/// The cycle may run
	bool CanExecute(unsigned long cycle) const;
	/// The game has not begun: no cycle runs and no command of the local player is taken
	bool Held() const { return !model.IsReleased(); }
	/// The own load is done
	bool Loaded() const { return model.IsLoaded(); }
	/// The local player has left and this device still answers the requests of the others
	bool Lingering() const { return model.IsLingering(); }
	/// The game is paused for everybody: no cycle runs
	bool Paused() const { return model.IsPaused(); }
	/// Player number of the one who paused, -1 while the game is not paused
	int PausedBy() const { return model.IsPaused() ? PlayerOfSlot(model.PausedBy()) : -1; }
	/// Who is waited for
	CNetworkWaitState WaitState(int64_t now) const;

	const mesh::Mesh &Model() const { return model; }

private:
	void Heard(int hostSlot);
	void Drain(bool ownPackets, int64_t now);

private:
	CMeshGlueEngine &engine;
	std::vector<int> slotPlayers;          /// player number per host slot
	int ownSlot;
	mesh::Mesh model;
	unsigned int removedAtStart = 0;       /// players removed while the game was held, bit n = player n
	int64_t waitAt[mesh::kMaxPlayers] = {}; /// time of the last waiting event per host slot, 0 = none
	int32_t waitLeft[mesh::kMaxPlayers] = {}; /// its time to the bound
	bool selfOut = false;                  /// this device is out of the game, not by leaving
	mesh::Reason selfOutReason = mesh::Reason::Lost;
};

//@}

#endif // !__NET_MESH_GLUE_H__
