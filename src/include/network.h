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
/**@name network.h - The network header file. */
//
//      (c) Copyright 1998-2007 by Lutz Sammer, Russell Smith, and Jimmy Salmon
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

#ifndef __NETWORK_H__
#define __NETWORK_H__

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "network/netsockets.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

class CUnit;
class CUnitType;

namespace mesh
{
struct Msg;
}

enum class EFlushMode;

class CNetworkParameter
{
public:
	CNetworkParameter();
	void FixValues();
public:
	std::string localHost;  /// Local network address to use
	unsigned int localPort; /// Local network port to use
	unsigned int gameCyclesPerUpdate;  /// Network update each # game cycles
	unsigned int NetworkLag;      /// Network lag (# update cycles)
	unsigned int timeoutInS;      /// Number of seconds until player times out

public:
	static const int defaultPort = 6660; /// Default communication port
public:
	static CNetworkParameter Instance;
};

/**
**  Who a mesh game waits for. Player is -1 while it waits for nobody.
**  The masks count in player numbers: bit n = player n.
*/
class CNetworkWaitState
{
public:
	CNetworkWaitState() { MsLeftOf.fill(-1); }

	int Player = -1;         /// Player number of the awaited player
	int MsLeft = 0;          /// ms until the devices give him up
	int AllowanceMsLeft = 0; /// ms of absence he has left in this game
	bool Removing = false;   /// His time is over, the devices agree on the cycle of his removal
	bool SelfOut = false;    /// This device is out of the game (connection lost)
	int SelfOutReason = 0;   /// mesh::Reason of that
	bool Held = false;       /// The game has not begun: the devices load, no cycle runs, no command is taken
	bool Loaded = false;     /// This device has finished its own load
	unsigned int NotLoaded = 0;      /// Held: the players who have not reported their load
	unsigned int NoSignal = 0;       /// Held: those of them who are silent (no "still loading" either)
	unsigned int RemovedAtStart = 0; /// The players removed while the game was held, as this device saw it
	bool Paused = false;     /// The game is paused for everybody: no cycle runs on any device
	int PausedBy = -1;       /// Player number of the one who paused
	std::array<int, 16> MsLeftOf;    /// Per player number: ms until the devices give him up, -1 = not awaited
};

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

extern CUDPSocket NetworkFildes;  /// Network file descriptor
extern bool NetworkInSync;        /// Network is in sync

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

extern inline bool IsNetworkGame() { return NetworkFildes.IsValid(); }
extern void InitNetwork1();  /// Initialise network
extern void ExitNetwork1();  /// Cleanup network (port)
extern void NetworkOnStartGame();  /// Initialise network data for ingame communication
extern void NetworkLoadingPing();  /// A game loads: tell the other devices that this one is alive (mesh)
extern void NetworkGameLoaded();   /// The own load of the game is done (mesh: the others are told, the wait for them begins)
extern bool NetworkIsHeld();       /// Mesh: the game has not begun on every device, no cycle runs and no command is taken
/// This device only watches the game from now on (its player is defeated): no order and no
/// diplomacy leaves it, the whole map is drawn. Nothing of the game's state is changed.
extern void NetworkSetWatching(bool on);
extern bool NetworkIsWatching();   /// This device only watches the game
extern void NetworkEvent();  /// Handle network events
extern void NetworkSync();   /// Hold in sync
extern void NetworkQuitGame();  /// Quit game: warn other users
extern void NetworkRecover();   /// Recover network
extern void NetworkCommands();  /// Get all network commands
extern void NetworkSendChatMessage(const std::string &msg);  /// Send chat message
/// Send network command.
extern void NetworkSendCommand(int command,
                               const CUnit &unit,
                               int x,
                               int y,
                               const CUnit *dest,
                               const CUnitType *type,
                               EFlushMode flush);
/// Send extended network command.
extern void NetworkSendExtendedCommand(int command, int arg1, int arg2,
									   int arg3, int arg4, int status);
/// Send Selections to Team
extern void NetworkSendSelection(CUnit **units, int count);

/// Send a mesh message to the device in a host slot
extern bool NetworkSendMesh(int hostIndex, const mesh::Msg &message);
/// Send a mesh message to every other device
extern void NetworkBroadcastMesh(const mesh::Msg &message);
/// Who a mesh game waits for
extern CNetworkWaitState NetworkGetWaitState();
/// Why a player is out of the network game (ENetQuitReason), -1 while his quit has not run
extern int NetworkGetQuitReason(int player);
/**
**  The wait as the scripts are told it (Lua NetWaitState). Built from CNetworkWaitState;
**  the names are those of the players.
*/
class CNetWaitPlayer
{
public:
	int Player = -1;    /// Player number
	std::string Name;   /// His name
	std::string State;  /// "loading", "silent" or "removing"
	int MsLeft = -1;    /// ms until the devices give him up, -1 = no bound runs
};

class CNetWaitView
{
public:
	std::string Phase = "none";  /// "none", "start" (the game has not begun) or "game"
	bool Held = false;           /// The game has not begun on every device
	bool SelfLoaded = false;     /// This device has finished its own load
	bool SelfOut = false;        /// This device is out of the game
	std::string SelfOutReason;   /// "connection_lost", "isolated" or "desync"
	int SelfOutMs = 0;           /// ms since this device is known to be out
	int Player = -1;             /// In the game: the awaited player, -1 = nobody
	std::string Name;            /// His name
	int MsLeft = 0;              /// ms until the devices give him up
	int AllowanceMsLeft = 0;     /// ms of absence he has left in this game
	bool Removing = false;       /// His time is over, his removal is being agreed
	bool Paused = false;         /// The game is paused for everybody
	int PausedBy = -1;           /// Who paused
	std::string PausedByName;    /// His name
	std::vector<CNetWaitPlayer> Players; /// Held: the players not loaded. In the game: the awaited one
	std::vector<int> RemovedAtStart;     /// Players removed while the game was held
	bool Fake = false;           /// A made-up state of the debug bridge, not the network's
};

/// The wait for the scripts, from a wait state. nameOf gives a player's name.
extern CNetWaitView NetworkBuildWaitView(const CNetworkWaitState &state,
										 const std::function<std::string(int)> &nameOf);
/// The wait for the scripts: the network's, or the made-up one while one is set
extern CNetWaitView NetworkGetWaitView();
/// Debug: make NetworkGetWaitView return this state, counting down in real time; nullptr = the network's again
extern void NetworkSetFakeWaitView(const CNetWaitView *view);
/// Debug: a made-up wait state is set
extern bool NetworkWaitViewFaked();
/// Once per drawn frame of a game: let the scripts follow the wait (Lua NetWaitFrame)
extern void NetworkWaitFrame();
/// The scripts follow the wait and show it themselves
extern bool NetworkWaitScripted();
/// Mesh: the bounds of an absence and of a slow load in seconds for the games started from now on, 0 = the default
extern void NetworkSetMeshTimeouts(int dropSeconds, int loadSeconds);
/// Execute the quit of a player: the end of a quit of the command stream
extern void NetworkExecQuit(int player, int reason);
/// Execute the pause of a player: the end of a pause command of the command stream (mesh)
extern void NetworkExecPause(int player);
/// The local player presses Pause: pause the game for everybody, or let a paused game go on (mesh). False if nothing was done
extern bool NetworkTogglePause();
/// Mesh: the game is paused for everybody
extern bool NetworkIsPaused();
/// A chat line as it may travel: no controls, no spaces at its ends, at most 120 characters; empty = no line
extern std::string NetworkChatText(const std::string &raw);
/// The local player says a chat line to everybody. Mesh: at once, also in a pause. False if nothing was sent
extern bool NetworkChat(const std::string &raw);
/// Chat lines go beside the command stream (a mesh game)
extern bool NetworkChatIsDirect();
/// Once per drawn frame of a game: tell the host whether there is a chat, take what its player typed
extern void NetworkChatFrame();

extern void NetworkCclRegister();

//@}

#endif // !__NETWORK_H__
