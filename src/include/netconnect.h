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
/**@name netconnect.h - The network connection setup header file. */
//
//      (c) Copyright 1998-2008 by Lutz Sammer, Andreas Arens, and Jimmy Salmon
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

#ifndef __NETCONNECT_H__
#define __NETCONNECT_H__

//@{

#include "net_message.h"
#include "network/netsockets.h"

#include "filesystem.h"

#include <set>
#include <string>
#include <string_view>
#include <cstdint>
#include <utility>
#include <vector>

class CHost;

/*----------------------------------------------------------------------------
--  Defines
----------------------------------------------------------------------------*/

/// Network protocol major version
#define NetworkProtocolMajorVersion StratagusMajorVersion
/// Network protocol minor version (maximum 99)
#define NetworkProtocolMinorVersion StratagusMinorVersion
/// Network protocol patch level (maximum 99). Above the engine's: +1 since the Config message
/// grew by the mesh byte and MessageMesh exists, +2 since the State message carries the mesh
/// byte and the Config ack the probe mask, +3 since a mesh game runs with the model (relay on
/// request, removal round), +4 since the quit command carries its reason and a mesh game
/// begins behind the start barrier, +5 since a pause is a command of the stream and a mesh
/// message (a pause for everybody), +6 since a chat line of a mesh game is a mesh message
/// of its own, +7 since every game is a mesh game: the Config and the State carry no mesh
/// byte and the server passes nothing on, +8 since sight is counted from the unit's centre
/// tile and a building under construction lights with range 3 (simulation state on every
/// device), +12 since an attack range against a unit is counted as the larger axis gap
/// (simulation state on every device), +14 since a missile comes down no nearer than its
/// unit type's minimal impact distance (simulation state on every device), +15 since the sync
/// hash also mixes each unit's tile, HP and its current order's goal, range and flags. A build
/// without them is refused at Hello.
#define NetworkProtocolPatchLevel   (StratagusPatchLevel + 15)
/// Network protocol version (1,2,3) -> 10203
#define NetworkProtocolVersion \
	(NetworkProtocolMajorVersion * 10000 + NetworkProtocolMinorVersion * 100 + \
	 NetworkProtocolPatchLevel)

/// Network protocol printf format string
#define NetworkProtocolFormatString "%d.%d.%d"
/// Network protocol printf format arguments
#define NetworkProtocolFormatArgs(v) (v) / 10000, ((v) / 100) % 100, (v) % 100

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

/**
**  Network Client connect states
*/
enum _net_client_con_state_ {
	ccs_unused = 0,           /// Unused.
	ccs_connecting,           /// New client
	ccs_connected,            /// Has received slot info
	ccs_mapinfo,              /// Has received matching map-info
	ccs_badmap,               /// Has received non-matching map-info
	ccs_synced,               /// Client is in sync with server
	ccs_async,                /// Server user has changed selection
	ccs_changed,              /// Client user has made menu selection
	ccs_detaching,            /// Client user wants to detach
	ccs_disconnected,         /// Client has detached
	ccs_unreachable,          /// Server is unreachable
	ccs_usercanceled,         /// Connection canceled by user
	ccs_nofreeslots,          /// Server has no more free slots
	ccs_serverquits,          /// Server quits
	ccs_goahead,              /// Server wants to start game
	ccs_started,              /// Server has started game
	ccs_incompatibleengine,   /// Incompatible engine version
	ccs_incompatibleluafiles, /// Incompatible lua files
	ccs_needmap,              /// Client needs to be sent the map
};

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

extern int NetPlayers;                /// Network players

extern CNetworkHost Hosts[PlayerMax]; /// Host, port, and number of all players.

extern int NetConnectRunning;              /// Network menu: Setup mode active
extern int NetConnectType;              /// Network menu: Setup mode active
extern int NetLocalHostsSlot;              /// Network menu: Slot # in Hosts array of local client
extern int NetLocalPlayerNumber;           /// Player number of local client

extern std::string NetworkMapName;         /// Name of the map received with ICMMap
extern std::string NetworkMapFragmentName; /// Name of the map currently loading via ICMMapNeeded

extern CServerSetup ServerSetupState;      /// Network menu: Multiplayer Server Menu selections state
extern CServerSetup LocalSetupState;       /// Network menu: Multiplayer Client Menu selections local state

extern bool NoRandomPlacementMultiplayer; /// Disable the random placement of players in multiplayer mode

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

extern int FindHostIndexBy(const CHost &host);
extern bool NetworkIsDedicatedServerHost(const CNetworkHost &host, const CServerSetup &setup);
/// Map transfer: a received file name stays under the library path and is a map file
extern bool IsSafeMapTransferName(std::string_view name);
/// Map transfer, sender: the files of `mapDirectory` whose name without its extensions is
/// `prefix`, sorted. A directory that is missing or cannot be read gives none (no throw).
extern std::set<fs::path> MapTransferFiles(const fs::path &mapDirectory, const fs::path &prefix);
/// Map transfer, sender: `file`'s name relative to `libPath` with '/' separators, or empty when
/// the file is not under `libPath`.
extern std::string MapTransferNetworkName(const fs::path &libPath, const fs::path &file);
extern int NetworkGetPlayerIndexForHost(int hostIndex);
extern bool NetworkIsLocalSetupInSync(const CServerSetup &state1,
                                      const CServerSetup &state2,
                                      int hostIndex);
extern void NetworkApplyClientSetupStateChange(CServerSetup &serverSetup,
                                               const CServerSetup &clientSetup,
                                               int hostIndex);
extern void NetworkCompactHosts();
extern std::vector<int> NetworkRemoteHostIndices();
extern bool NetworkServerStartGame();       /// Server user has finally hit the start game button
extern void NetworkGamePrepareGameSettings();

extern int GetNetworkState();

extern void NetworkInitClientConnect();     /// Setup network connect state machine for clients
extern void NetworkInitServerConnect(int openslots); /// Setup network connect state machine for the server
extern bool NetworkParseSetupEvent(const unsigned char *buf, int size, const CHost &host);  /// Parse a network connect event
extern int NetworkSetupServerAddress(const std::string &serveraddr, int port);  /// Menu: Setup the server IP
extern void NetworkProcessClientRequest();  /// Menu Loop: Send out client request messages
extern void NetworkProcessServerRequest();  /// Menu Loop: Send out server request messages
extern void NetworkServerResyncClients();   /// Menu Loop: Server: Mark clients state to send state info message
extern bool NetworkServerClientsSynced();   /// Menu Loop: Server: all connected clients are in sync
extern void NetworkDetachFromServer();      /// Menu Loop: Client: Send GoodBye to the server and detach

extern void NetworkSendICMessage(CUDPSocket &socket, const CHost &host, const CInitMessage_Header &msg);

extern const std::string &NetworkServerStartFailure(); /// Server: why the last start was given up
extern void NetworkMeshProbeAnswered(int hostIndex); /// Client: the peer in that host slot answered our probe
extern int NetworkClientUnreachedPeers(); /// Client: other clients that have not answered our probe

/// NET-CONTENT-ID. The game-folder files the simulation reads (src/stratagus/wc2data.cpp,
/// src/ai/wc2ai.cpp); the same list as the launcher's LanIdentity.GAME_DATA_FILES.
extern const std::vector<std::string> &NetworkContentDataFiles();
/// Content id of the given files: every file under scriptsDir in the order of its path relative
/// to it, then the data files in their given order (name -> the path found for it); each name,
/// length and bytes go in, a missing file goes in as missing.
extern uint32_t NetworkContentIdOf(const fs::path &scriptsDir,
                                   const std::vector<std::pair<std::string, fs::path>> &dataFiles);
/// What two devices compare in the hello (the "lua files" check): FileChecksums (stratagus.lua),
/// the scripts of <lib>/scripts and the game folder's simulation data. Computed once.
extern uint32_t NetworkContentVersion();
/// Forget the computed content version (tests; a changed game folder).
extern void NetworkContentVersionReset();

//@}

#endif // !__NETCONNECT_H__
