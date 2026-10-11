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
/**@name network_test_hooks.h - Entries into network.cpp for the unit tests. */
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

#ifndef __NETWORK_TEST_HOOKS_H__
#define __NETWORK_TEST_HOOKS_H__

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "network/netsockets.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace mesh
{
struct Msg;
struct Params;
}
class CMeshGlue;

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

// The in-game packet parser and its command store are static in network.cpp.
// The game does not call these functions.

/// Parse an in-game packet as if it had been received from host
extern void NetworkTestParseInGameEvent(const unsigned char *buf, int len, const CHost &host);
/// Get the stored command of a player for a cycle, false if the store holds none
extern bool NetworkTestGetCommand(unsigned long cycle, int player, int index,
                                  unsigned char &type, std::vector<unsigned char> &data);
/// Time out a player now, without the wait and without the message on the screen
extern void NetworkTestTimeOutPlayer(int player);
/// Player is marked as quit
extern bool NetworkTestPlayerQuit(int player);
/// Handle a mesh datagram as if it had been received from host
extern void NetworkTestParseMeshEvent(const unsigned char *buf, int len, const CHost &host);
/// Take the mesh messages accepted since the last call: (host slot of the sender, message)
extern std::vector<std::pair<int, mesh::Msg>> NetworkTestTakeMeshMessages();
/// Number of mesh datagrams dropped
extern unsigned int NetworkTestMeshDropped();
/// Start the model of a mesh game with the given time bounds, false if the game is none for it
extern bool NetworkTestMeshStart(const mesh::Params &params);
/// End the model of the mesh game
extern void NetworkTestMeshStop();
/// What NetworkRecover does in a mesh game, without the text on the screen
extern void NetworkTestMeshUpdate();
/// The model of the running mesh game, null if there is none
extern const CMeshGlue *NetworkTestMeshGlue();

// The client of the setup and the results of the peer probe are static in netconnect.cpp.

/// Put the client of the setup into a state (ccs_...) without the messages which lead there
extern void NetworkTestSetClientState(int state);
/// Client: the peers which answered the probe, bit n = host slot n
extern uint16_t NetworkTestMeshProbeMask();
/// Server: the pairs (a, b) of the last start where slot a got no answer from slot b
extern std::vector<std::pair<int, int>> NetworkTestMeshProbeFailed();

//@}

#endif // !__NETWORK_TEST_HOOKS_H__
