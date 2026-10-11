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
/**@name network.cpp - The network. */
//
//      (c) Copyright 2000-2008 by Lutz Sammer, Andreas Arens, and Jimmy Salmon
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

//----------------------------------------------------------------------------
// Documentation
//----------------------------------------------------------------------------

/**
** @page NetworkModule Module - Network
**
** @section Basics How does it work.
**
** Stratagus uses an UDP peer to peer protocol (p2p). The default port
** is 6660.
**
** @subsection udp_vs_tcp UDP vs. TCP
**
** UDP is a connectionless protocol. This means it does not perform
** retransmission of data and therefore provides very few error recovery
** services. UDP instead offers a direct way to send and receive
** datagrams (packets) over the network; it is used primarily for
** broadcasting messages.
**
** TCP, on the other hand, provides a connection-based, reliable data
** stream. TCP guarantees delivery of data and also guarantees that
** packets will be delivered in the same order in which they were sent.
**
** TCP is a simple and effective way of transmitting data. For making sure
** that a client and server can talk to each other it is very good.
** However, it carries with it a lot of overhead and extra network lag.
**
** UDP needs less overhead and has a smaller lag. Which is very important
** for real time games. The disadvantages includes:
**
** @li You won't have an individual socket for each client.
** @li Given that clients don't need to open a unique socket in order to
** transmit data there is the very real possibility that a client
** who is not logged into the game will start sending all kinds of
** garbage to your server in some kind of attack. It becomes much
** more difficult to stop them at this point.
** @li Likewise, you won't have a clear disconnect/leave game message
** unless you write one yourself.
** @li Some data may not reach the other machine, so you may have to send
** important stuff many times.
** @li Some data may arrive in the wrong order. Imagine that you get
** package 3 before package 1. Even a package can come duplicate.
** @li UDP is connectionless and therefore has problems with firewalls.
**
** I have chosen UDP. Additional support for the TCP protocol is welcome.
**
** @subsection sc_vs_p2p server/client vs. peer to peer
**
** @li server to client
**
** The player input is send to the server. The server collects the input
** of all players and than send the commands to all clients.
**
** @li peer to peer (p2p)
**
** The player input is direct send to all others clients in game.
**
** p2p has the advantage of a smaller lag, but needs a higher bandwidth
** by the clients.
**
** p2p has been chosen for in-game.
** s/c for the preparing room.
**
** @subsection bandwidth bandwidth
**
** I wanted to support up to 8 players with 28.8kbit modems.
**
** Most modems have a bandwidth of 28.8K bits/second (both directions) to
** 56K bits/second (33.6K uplink) It takes actually 10 bits to send 1 byte.
** This makes calculating how many bytes you are sending easy however, as
** you just need to divide 28800 bits/second by 10 and end up with 2880
** bytes per second.
**
** We want to send many packets, more updated per second and big packets,
** less protocol overhead.
**
** If we do an update 6 times per second, leaving approximately 480 bytes
** per update in an ideal environment.
**
** For the TCP/IP protocol we need following:
** IP  Header 20 bytes
** UDP Header 8  bytes
**
** With ~9 bytes per command and up to N(=9) commands there are 20+8+1+9*N(=120) bytes
** per packet. Sending it to 7 other players, gives 840 bytes per update.
** This means we could do 6 updates (each 166ms) per second (6*840=5040 bytes/s).
**
** @subsection a_packet Network packet
**
** @li [IP  Header - 20 bytes]
** @li [UDP Header -  8 bytes]
** @li [Header Data:Type - 1 byte]
** if Type is one of the InitConfigMessage
** @li [Header Data:SubType - 1 byte]
** @li [Data - depend of subtype (may be 0 byte)]
** else
** @li [Header Data:Types - N-1 bytes] (for N commands)
** @li [Header Data:Cycle - 1 byte]
** @li [Data:Commands - Sum of Xi bytes for the N Commands]
**
**
** @subsection internals Putting it together
**
** All computers in play must run absolute syncron.
** Only user commands are send over the network to the other computers.
** To reduce network traffic, commands are sent/executed every gameCyclesPerUpdate
** gameCycles. The command needs some time to reach the other clients (lag),
** so the command is not executed immediately on the local computer,
** but a delay (NetworkLag NetUpdates) later. Commands are stored
** in a circular array indexed by update time. Once each other players commands
** are received for a specified gameNetCycle, all commands of this gameNetCycle
** Each gameNetCycle, a package must be send. if there is no user command,
** a "dummy" sync package is send (which checks that all players are still in sync).
** If there are missing packages, the game is paused and old commands
** are resend to all clients.
**
** @section missing What features are missing
**
** @li The recover from lost packets can be improved, as the player knows
** which packets is missing.
**
** @li The UDP protocol isn't good for firewalls, we need also support
** for the TCP protocol.
**
** @li Add a server/client protocol, which allows more players per game.
**
** @li Lag (latency) and bandwidth should be automatic detected during game setup
** and later during game automatic adapted.
**
** @li Also it would be nice, if we support viewing clients. This means
** other people can view the game in progress.
**
** @li The current protocol only uses single cast, for local LAN we
** should also support broadcast and multicast.
**
** @li Proxy and relays should be supported, to improve the playable
** over the internet.
**
** @li We can sort the command by importants, currently all commands are
** send in order, only chat messages are send if there are free slots.
**
** @li password protection the login process (optional), to prevent that
** the wrong player join an open network game.
**
** @li add meta server support, I have planned to use bnetd and its protocol.
**
** @section api API How should it be used.
**
** ::InitNetwork1()
** Open port. Must be called by Lua
**
** ::ExitNetwork1()
** Close port. Called internally and by Lua
**
** ::NetworkOnGameStart()
** Initialize msg stuff for ingame communication.
**
** ::NetworkEvent()
** Manage network event (preparation room + ingame).
**
** ::NetworkRecover()
** Do stuff to have again NetworkInSync == true
**
** ::NetworkCommands()
** Network Updates : exec current command, and send commands to other players
**
** ::NetworkFildes
** UDP Socket for communication.
**
** ::NetworkInSync
** false when commands of the next gameNetCycle of the other player are not ready.
**
** ::NetworkSendCommand()
** Send a normal unit order.
**
** ::NetworkSendExtendedCommand()
** Send special command (diplomacy, ...)
**
** ::NetworkSendChatMessage()
** Send a chat message to others player
**
** ::NetworkQuitGame()
** Warn other users that we leave.
*/

//----------------------------------------------------------------------------
//  Includes
//----------------------------------------------------------------------------

#include "online_service.h"
#include "stratagus.h"

#include "network.h"
#include "network_test_hooks.h"

#include "actions.h"
#include "commands.h"
#include "interface.h"
#include "map.h"
#include "net_lowlevel.h"
#include "net_mesh_glue.h"
#include "net_mesh_wire.h"
#include "net_message.h"
#include "netconnect.h"
#include "parameters.h"
#include "player.h"
#include "replay.h"
#include "sound.h"
#include "translate.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"
#include "video.h"

#include <array>
#include <cstddef>
#include <deque>
#include <list>
#include <memory>

//----------------------------------------------------------------------------
//  Declaration
//----------------------------------------------------------------------------

extern int SaveGame(const std::string &filename); /// Save game

/**
**  Network command input/output queue.
*/
class CNetworkCommandQueue
{
public:
	CNetworkCommandQueue() : Time(0), Type(0) {}
	void Clear() { this->Time = this->Type = 0; Data.clear(); }

	bool operator == (const CNetworkCommandQueue &rhs) const
	{
		return Time == rhs.Time && Type == rhs.Type && Data == rhs.Data;
	}
	bool operator != (const CNetworkCommandQueue &rhs) const { return !(*this == rhs); }
public:
	unsigned long Time;    /// time to execute
	unsigned char Type;    /// Command Type
	std::vector<unsigned char> Data;  /// command content (network format)
};

//----------------------------------------------------------------------------
//  Variables
//----------------------------------------------------------------------------

/* static */ CNetworkParameter CNetworkParameter::Instance;

CNetworkParameter::CNetworkParameter()
{
	localHost = "127.0.0.1";
	localPort = defaultPort;
	gameCyclesPerUpdate = 1;
	NetworkLag = 10;
	timeoutInS = 45;
}

void CNetworkParameter::FixValues()
{
	gameCyclesPerUpdate = std::max(gameCyclesPerUpdate, 1u);
	NetworkLag = std::max(NetworkLag, 2u * gameCyclesPerUpdate);
}

bool NetworkInSync = true;                 /// Network is in sync

CUDPSocket NetworkFildes;                  /// Network file descriptor

static unsigned long NetworkLastTick[PlayerMax];  /// Last tick (ms) received packet, 0 = never
static unsigned long NetworkLastCycle[PlayerMax]; /// Last cycle received packet

static unsigned int NetworkSyncSeeds[256];          /// Network sync seeds.
static unsigned int NetworkSyncHashs[256];          /// Network sync hashs.
static unsigned long NetworkSyncCycles[256];        /// Game cycle sampled for sync data.
static unsigned long NetworkFirstDesyncGameCycle;   /// First cycle that detected a desync.
static unsigned long NetworkFirstDesyncSampleCycle; /// First sampled cycle that diverged.
static bool NetworkGameInSync = true;               /// Last sync command comparison result.
static CNetworkCommandQueue NetworkIn[256][PlayerMax][MaxNetworkCommands]; /// Per-player network packet input queue
static std::deque<CNetworkCommandQueue> CommandsIn;    /// Network command input queue
static std::deque<CNetworkCommandQueue> MsgCommandsIn; /// Network message input queue


#ifdef DEBUG
class CNetworkStat
{
public:
	CNetworkStat() :
		resentPacketCount(0)
	{}

	void print() const
	{
		DebugPrint("resent: %d packets\n", resentPacketCount);
	}

public:
	unsigned int resentPacketCount;
};

static void printStatistic(const CUDPSocket::CStatistic &statistic)
{
	DebugPrint("Sent: %d packets %d bytes (max %d bytes).\n",
	           statistic.sentPacketsCount,
	           statistic.sentBytesCount,
	           statistic.biggestSentPacketSize);
	DebugPrint("Received: %d packets %d bytes (max %d bytes).\n",
	           statistic.receivedPacketsCount,
	           statistic.receivedBytesCount,
	           statistic.biggestReceivedPacketSize);
	DebugPrint("Received: %d error(s).\n", statistic.receivedErrorCount);
}

static CNetworkStat NetworkStat;
#endif

static int PlayerQuit[PlayerMax];          /// Player quit
static int PlayerGone[PlayerMax];          /// Player quit has been executed
static int PlayerQuitReason[PlayerMax];    /// Why (ENetQuitReason), valid once the quit has been executed
static bool NetworkQuitAnnounced;          /// The local player has told the others of this game that he leaves
static int NetworkPauseAsked = -1;         /// Player whose pause command ran in the cycle being executed, -1 = none

static unsigned int NetworkMeshDropped;    /// Mesh datagrams dropped (not valid, or not from the address of the sender)
static std::deque<std::pair<int, mesh::Msg>> NetworkMeshReceived; /// The last mesh messages accepted, for the tests

/**
**  The engine as the mesh model sees it (net_mesh_glue.h).
*/
class CNetworkMeshEngine : public CMeshGlueEngine
{
public:
	void SendMesh(int hostSlot, const mesh::Msg &message) override;
	void SendPacket(int hostSlot, int player, unsigned long cycle) override;
	void InstallQuit(int player, unsigned long cycle, mesh::Reason reason) override;
	void SetPaused(bool paused, int player) override;
};

static CNetworkMeshEngine NetworkMeshEngine;
/// The model of the running mesh game, none in a star game and outside a game
static std::unique_ptr<CMeshGlue> MeshGlue;

/// A device which loads tells the others so at this interval (ms)
static constexpr unsigned long MeshLoadingPingMs = 250;
/// A device which has left repeats its quit at this interval (ms) while it still answers
static constexpr unsigned long MeshQuitRepeatMs = 100;
/// It reads its socket in steps of this length (ms) meanwhile
static constexpr int MeshLingerPollMs = 10;
/// and is gone after this time (ms) whatever the model says
static constexpr unsigned long MeshLingerMaxMs = 5000;

static unsigned long NetworkTicks();
static bool IsNetworkCycleReady(unsigned long gameNetCycle);
static void NetworkParseInGameEvent(const unsigned char *buf, int len, const CHost &host, bool relayed = false);

//----------------------------------------------------------------------------
//  Mid-Level api functions
//----------------------------------------------------------------------------

/**
**  Send message to all clients.
**
**  @param packet       Packet to send.
**  @param numcommands  Number of commands.
*/
static void NetworkBroadcast(const CNetworkPacket &packet, int numcommands, int player = 255)
{
	const unsigned int size = packet.Size(numcommands);
	std::vector<unsigned char> buf(size);
	packet.Serialize(buf.data(), numcommands);

	// Send to all clients.
	if (NetMesh) { // every device sends to every other device
		for (int i = 0; i < PlayerMax; ++i) {
			if (i == NetLocalHostsSlot) {
				continue; // skip self
			}
			if (Hosts[i].IsValid()) {
				const CHost host(Hosts[i].Host, Hosts[i].Port);
				if (Hosts[i].PlyNr == player) {
					continue;
				}
				NetworkFildes.Send(host, buf.data(), buf.size());
			}
		}
	} else if (NetConnectType == 1) { // server
		for (int i = 0; i < NetPlayers; ++i) {
			if (i == NetLocalHostsSlot) {
				continue; // skip self
			}
			if (Hosts[i].IsValid()) {
				const CHost host(Hosts[i].Host, Hosts[i].Port);
				if (Hosts[i].PlyNr == player) {
					continue;
				}
				NetworkFildes.Send(host, buf.data(), buf.size());
			}
		}
	} else { // client
		const CHost host(Hosts[0].Host, Hosts[0].Port);
		NetworkFildes.Send(host, buf.data(), buf.size());
	}
}

/**
**  Send message to one host.
**
**  @param host         Host to send to.
**  @param packet       Packet to send.
**  @param numcommands  Number of commands.
*/
static void NetworkSendTo(const CHost &host, const CNetworkPacket &packet, int numcommands)
{
	const unsigned int size = packet.Size(numcommands);
	std::vector<unsigned char> buf(size);
	packet.Serialize(buf.data(), numcommands);

	NetworkFildes.Send(host, buf.data(), buf.size());
}

/**
**  Send a mesh message to the device in a host slot.
**
**  @param hostIndex  Host slot of the receiver.
**  @param message    Message; its players are host slots (net_mesh_wire.h).
**
**  @return  false if there is no such device or the message cannot be sent.
*/
bool NetworkSendMesh(int hostIndex, const mesh::Msg &message)
{
	if (hostIndex < 0 || hostIndex >= PlayerMax || hostIndex == NetLocalHostsSlot
		|| !Hosts[hostIndex].IsValid()) {
		return false;
	}
	unsigned char buf[mesh::kWireMaxSize];
	const size_t size = mesh::Serialize(message, NetLocalHostsSlot, buf, sizeof(buf));
	if (size == 0) {
		DebugPrint("Mesh message of kind %d cannot be sent\n", int(message.kind));
		return false;
	}
	const CHost host(Hosts[hostIndex].Host, Hosts[hostIndex].Port);
	NetworkFildes.Send(host, buf, size);
	return true;
}

/**
**  Send a mesh message to every other device.
**
**  @param message  Message; its players are host slots (net_mesh_wire.h).
*/
void NetworkBroadcastMesh(const mesh::Msg &message)
{
	for (int i = 0; i < PlayerMax; ++i) {
		NetworkSendMesh(i, message);
	}
}

/**
**  A mesh message of another device has arrived.
**
**  @param hostIndex  Host slot of the sender, checked against the address it came from.
**  @param message    Message; its players are host slots (net_mesh_wire.h).
**
**  It goes to the model; the last ones are kept for the tests.
*/
static void NetworkMeshMessage(int hostIndex, const mesh::Msg &message)
{
	if (NetworkMeshReceived.size() == 64) {
		NetworkMeshReceived.pop_front();
	}
	NetworkMeshReceived.emplace_back(hostIndex, message);
	if (MeshGlue) {
		// The model answers in its tick; a device whose game stands (pause) still has to answer.
		const unsigned long now = NetworkTicks();
		MeshGlue->OnMeshMessage(hostIndex, message, now);
		MeshGlue->Tick(now);
	}
}

/**
**  Parse a mesh datagram (first byte MessageMesh): a probe of the setup or a message
**  of the model. It is taken only from the address of the host slot it names.
**
**  @param buf   Datagram.
**  @param len   Size received.
**  @param host  Host the datagram comes from.
*/
static void NetworkParseMeshEvent(const unsigned char *buf, int len, const CHost &host)
{
	const int index = FindHostIndexBy(host);
	const size_t size = len < 0 ? 0 : len;
	bool ack = false;
	int from = -1;

	if (mesh::DeserializeProbe(buf, size, &ack, &from)) {
		// Probes travel before the game, when the mesh is not on yet.
		if (index == -1 || index != from || index == NetLocalHostsSlot || !Hosts[index].IsValid()) {
			++NetworkMeshDropped;
			return;
		}
		if (ack) {
			NetworkMeshProbeAnswered(index);
		} else {
			unsigned char answer[mesh::kWireProbeSize];
			const size_t n = mesh::SerializeProbe(true, NetLocalHostsSlot, answer, sizeof(answer));
			if (n != 0) {
				NetworkFildes.Send(host, answer, n);
			}
		}
		return;
	}
	const unsigned char *relayed = nullptr;
	size_t relayedSize = 0;
	if (MeshGlue && mesh::DeserializeRelay(buf, size, &from, &relayed, &relayedSize)) {
		// The command packet of another player, passed on by a device which holds it.
		if (index == -1 || index != from || index == NetLocalHostsSlot || !Hosts[index].IsValid()
			|| relayedSize < CNetworkPacketHeader::Size()) {
			++NetworkMeshDropped;
			return;
		}
		NetworkParseInGameEvent(relayed, int(relayedSize), host, true);
		return;
	}
	mesh::Msg message;
	if (!NetMesh || !mesh::Deserialize(buf, size, &message, &from)
		|| index == -1 || index != from || index == NetLocalHostsSlot || !Hosts[index].IsValid()) {
#ifdef DEBUG
		const std::string hostStr = host.toString();
		DebugPrint("Mesh datagram of %d bytes from %s dropped\n", len, hostStr.c_str());
#endif
		++NetworkMeshDropped;
		return;
	}
	NetworkMeshMessage(index, message);
}

/**
**  Build network packet from queue.
**
**  @param ncq     Outgoing network queue start.
**  @param packet  Packet to build.
**
**  @return  Number of commands in the packet.
*/
static int NetworkBuildPacket(const CNetworkCommandQueue(&ncq)[MaxNetworkCommands], CNetworkPacket &packet)
{
	// Build packet of up to MaxNetworkCommands messages.
	int numcommands = 0;
	packet.Header.Cycle = ncq[0].Time & 0xFF;
	packet.Header.OrigPlayer = ThisPlayer->Index;
	int i;
	for (i = 0; i < MaxNetworkCommands && ncq[i].Type != MessageNone; ++i) {
		packet.Header.Type[i] = ncq[i].Type;
		packet.Command[i] = ncq[i].Data;
		++numcommands;
	}
	for (; i < MaxNetworkCommands; ++i) {
		packet.Header.Type[i] = MessageNone;
	}
	return numcommands;
}

/**
**  Network send packet. Build it from queue and broadcast.
**
**  @param ncq  Outgoing network queue start.
*/
static void NetworkSendPacket(const CNetworkCommandQueue(&ncq)[MaxNetworkCommands])
{
	CNetworkPacket packet;
	const int numcommands = NetworkBuildPacket(ncq, packet);

	NetworkBroadcast(packet, numcommands);
}

/**
**  Real time for the time-out of players.
**
**  @return  Ticks in ms, never 0 (0 is "never received a packet").
*/
static unsigned long NetworkTicks()
{
	return std::max(GetTicks(), 1ul);
}

//----------------------------------------------------------------------------
//  Mesh: the model beside the command queue (net_mesh_glue.h)
//----------------------------------------------------------------------------

void CNetworkMeshEngine::SendMesh(int hostSlot, const mesh::Msg &message)
{
	NetworkSendMesh(hostSlot, message);
}

/**
**  Send the held packet of a player for a cycle to one device: the own one as the
**  packet it is, the one of another player as a relay.
*/
void CNetworkMeshEngine::SendPacket(int hostSlot, int player, unsigned long cycle)
{
	if (hostSlot < 0 || hostSlot >= PlayerMax || hostSlot == NetLocalHostsSlot || !Hosts[hostSlot].IsValid()
		|| player < 0 || player >= PlayerMax || !ThisPlayer) {
		return;
	}
	const CNetworkCommandQueue(&ncq)[MaxNetworkCommands] = NetworkIn[cycle & 0xFF][player];
	if (ncq[0].Time != cycle) {
		return; // not held (any more)
	}
	const CHost host(Hosts[hostSlot].Host, Hosts[hostSlot].Port);
	CNetworkPacket packet;
	if (player == ThisPlayer->Index) {
		const int numcommands = NetworkBuildPacket(ncq, packet);
		NetworkSendTo(host, packet, numcommands);
		return;
	}
	// The packet as its origin sent it: the commands which were stored for that cycle.
	int numcommands = 0;
	packet.Header.Cycle = cycle & 0xFF;
	packet.Header.OrigPlayer = player;
	for (int i = 0; i < MaxNetworkCommands && ncq[i].Type != MessageNone; ++i) {
		if (ncq[i].Time != cycle) {
			continue;
		}
		packet.Header.Type[numcommands] = ncq[i].Type;
		packet.Command[numcommands] = ncq[i].Data;
		++numcommands;
	}
	for (int i = numcommands; i < MaxNetworkCommands; ++i) {
		packet.Header.Type[i] = MessageNone;
	}
	std::vector<unsigned char> bytes(packet.Size(numcommands));
	packet.Serialize(bytes.data(), numcommands);
	unsigned char buf[mesh::kWireRelayMaxSize];
	const size_t size = mesh::SerializeRelay(bytes.data(), bytes.size(), NetLocalHostsSlot, buf, sizeof(buf));
	if (size != 0) {
		NetworkFildes.Send(host, buf, size);
	}
}

/**
**  The devices agreed that a player is out from a cycle on: his quit goes into his
**  place of that cycle, where NetworkExecCommands finds it on every device.
*/
void CNetworkMeshEngine::InstallQuit(int player, unsigned long cycle, mesh::Reason reason)
{
	if (player < 0 || player >= PlayerMax) {
		return;
	}
	CNetworkCommandQueue(&ncq)[MaxNetworkCommands] = NetworkIn[cycle & 0xFF][player];
	PlayerQuit[player] = 1;
	if (ncq[0].Time == cycle) {
		for (int i = 0; i < MaxNetworkCommands && ncq[i].Type != MessageNone; ++i) {
			if (ncq[i].Time == cycle && ncq[i].Type == MessageQuit) {
				return; // he left himself, his own quit is held
			}
		}
	}
	// Whatever else is held for that cycle did not reach every device.
	CNetworkCommandQuit nc;
	nc.player = player;
	// The cycle is the same on every device, so the reason is. Lost at the first cycle a packet
	// of his was needed: nobody holds one, he has not run a cycle of this game.
	if (reason == mesh::Reason::Left) {
		nc.reason = NetQuitLeft;
	} else if (cycle <= CNetworkParameter::Instance.NetworkLag + 1) {
		nc.reason = NetQuitNeverStarted;
	} else {
		nc.reason = NetQuitLost;
	}
	ncq[0].Time = cycle;
	ncq[0].Type = MessageQuit;
	ncq[0].Data.resize(nc.Size());
	nc.Serialize(&ncq[0].Data[0]);
	for (int i = 1; i < MaxNetworkCommands; ++i) {
		ncq[i].Clear();
	}
}

/**
**  The game is paused for everybody, or goes on (NET_PAUSE_SPEC.md). The same on every
**  device: who did it is told in the message line, the scripts show the pause.
*/
void CNetworkMeshEngine::SetPaused(bool paused, int player)
{
	const char *name = (player >= 0 && player < PlayerMax) ? Players[player].Name.c_str() : "";

	GamePaused = paused;
	if (GameRunning) { // the message line belongs to a game on the screen
		if (paused) {
			SetMessage(_("Paused by %s"), name);
		} else {
			SetMessage(_("Resumed by %s"), name);
		}
	}
	ErrorPrint("NETPAUSE %s by player %d (%s) after cycle %lu\n",
	           paused ? "paused" : "resumed", player, name, GameCycle);
}

/**
**  The pause command of a player is executed. The model is told when the cycle is
**  done: the pause begins after it, on every device.
**
**  @param player  Player number.
*/
void NetworkExecPause(int player)
{
	if (MeshGlue && NetworkPauseAsked == -1) {
		NetworkPauseAsked = player;
	}
}

/**
**  The local player presses Pause. A game that runs is paused for everybody by a
**  command of the stream; a paused one goes on for everybody at once.
**
**  @return  False if nothing was done: no mesh game, the game has not begun, this device is out.
*/
bool NetworkTogglePause()
{
	if (!MeshGlue || !ThisPlayer || MeshGlue->Held() || MeshGlue->Lingering() || MeshGlue->Model().IsOut()) {
		return false;
	}
	if (MeshGlue->Paused()) {
		MeshGlue->OnResume(NetworkTicks());
		NetworkInSync = IsNetworkCycleReady(GameCycle + CNetworkParameter::Instance.gameCyclesPerUpdate);
	} else {
		NetworkSendExtendedCommand(ExtendedMessagePause, 0, ThisPlayer->Index, 0, 0, 0);
	}
	return true;
}

bool NetworkIsPaused()
{
	return MeshGlue && MeshGlue->Paused();
}

/**
**  Start the model of a mesh game. The game is a mesh with a model only if the table
**  of the devices is the one the model counts with; otherwise it stays as it was.
**
**  @param params  Time bounds of the model.
*/
static void NetworkMeshStart(const mesh::Params &params)
{
	MeshGlue.reset();
	if (!NetMesh || !IsNetworkGame() || !ThisPlayer) {
		return;
	}
	if (NetPlayers < 2 || NetPlayers > mesh::kMaxPlayers || NetLocalHostsSlot < 0 || NetLocalHostsSlot >= NetPlayers
		|| CNetworkParameter::Instance.gameCyclesPerUpdate != 1
		|| CNetworkParameter::Instance.NetworkLag != mesh::kLag) {
		DebugPrint("Mesh game without the model: %d hosts\n", NetPlayers);
		return;
	}
	// Host slot -> player number. The model and the wire count in host slots.
	std::vector<int> slotPlayers;
	for (int i = 0; i < NetPlayers; ++i) {
		if (i == NetLocalHostsSlot) {
			slotPlayers.push_back(ThisPlayer->Index);
		} else if (Hosts[i].IsValid()) {
			slotPlayers.push_back(Hosts[i].PlyNr);
		} else {
			DebugPrint("Mesh game without the model: host slot %d is empty\n", i);
			return;
		}
	}
	MeshGlue = std::make_unique<CMeshGlue>(NetworkMeshEngine, slotPlayers, NetLocalHostsSlot, params, NetworkTicks());
}

/**
**  The command queue has (or already had) the packet of a player for a cycle, or
**  something else came from a device: tell the model.
*/
static void NetworkMeshPacketSeen(int hostIndex, int player, unsigned long cycle, bool relayed)
{
	if (!MeshGlue) {
		return;
	}
	const CNetworkCommandQueue(&ncq)[MaxNetworkCommands] = NetworkIn[cycle & 0xFF][player];
	if (ncq[0].Time != cycle) {
		if (!relayed) {
			MeshGlue->OnHeard(hostIndex, NetworkTicks());
		}
		return;
	}
	bool quit = false;
	for (int i = 0; i < MaxNetworkCommands && ncq[i].Type != MessageNone; ++i) {
		quit = quit || (ncq[i].Time == cycle && ncq[i].Type == MessageQuit);
	}
	MeshGlue->OnPacket(hostIndex, player, cycle, quit, NetworkTicks());
}

/// Mesh: the bounds asked for by the scripts, in seconds; 0 = the model's default
static int MeshDropSeconds = 0;
static int MeshLoadSeconds = 0;

/**
**  Mesh: the bound of one absence and the bound of a live slow loader, for the games
**  started from now on.
**
**  @param dropSeconds  Seconds a silent player is waited for in the game, 0 = the default.
**  @param loadSeconds  Seconds a loading player is waited for at the start, 0 = the default.
*/
void NetworkSetMeshTimeouts(int dropSeconds, int loadSeconds)
{
	MeshDropSeconds = dropSeconds > 0 ? dropSeconds : 0;
	MeshLoadSeconds = loadSeconds > 0 ? loadSeconds : 0;
}

/**
**  Who a mesh game waits for.
*/
CNetworkWaitState NetworkGetWaitState()
{
	if (!MeshGlue) {
		return CNetworkWaitState();
	}
	return MeshGlue->WaitState(NetworkTicks());
}

/**
**  Why a player is out of the network game.
**
**  @param player  Player number.
**
**  @return  ENetQuitReason, -1 while his quit has not been executed.
*/
int NetworkGetQuitReason(int player)
{
	if (player < 0 || player >= PlayerMax || !PlayerGone[player]) {
		return -1;
	}
	return PlayerQuitReason[player];
}

/**
**  Mesh game: the game has not begun on every device (B.4). No cycle runs and no
**  command of the local player is taken until every device has loaded, or the ones
**  which did not are removed.
*/
bool NetworkIsHeld()
{
	return MeshGlue && MeshGlue->Held();
}

/**
**  The own load of the game is done and the game loop begins. Mesh game: from now
**  on the others are told so, and the wait for them has its bounds.
*/
void NetworkGameLoaded()
{
	if (MeshGlue) {
		MeshGlue->OnLoaded(NetworkTicks());
	}
}

/**
**  Called again and again while a game loads. Mesh game: tell the others that this
**  device is alive and still loads, so that they do not take it for a dead one.
**  Nothing is read here; what the others send waits on the socket for the game loop.
*/
void NetworkLoadingPing()
{
	static unsigned long lastPing;

	if (!NetMesh || !IsNetworkGame() || NetConnectRunning || GameRunning
		|| (MeshGlue && MeshGlue->Loaded())) {
		return;
	}
	const unsigned long now = NetworkTicks();
	if (now - lastPing < MeshLoadingPingMs) {
		return;
	}
	lastPing = now;
	if (MeshGlue) {
		MeshGlue->Tick(now); // before the end of the load the model sends its "still loading" only
	} else {
		// The map loads and the game is not set up yet: the same message without the model.
		mesh::Msg message;
		message.kind = mesh::Kind::Loading;
		NetworkBroadcastMesh(message);
	}
}

//----------------------------------------------------------------------------
//  API init..
//----------------------------------------------------------------------------

/**
**  Initialize network port.
*/
void InitNetwork1()
{
	if (NetworkFildes.IsValid()) {
		return;
	}
	CNetworkParameter::Instance.FixValues();

	NetInit(); // machine dependent setup

	// Our communication port
	const int port = CNetworkParameter::Instance.localPort;
	const CHost host("", port);
	NetworkFildes.Open(host);
	if (NetworkFildes.IsValid() == false) {
		ErrorPrint("NETWORK: No free port %d available, aborting\n", port);
		NetExit(); // machine dependent network exit
		return;
	}
#ifdef DEBUG
	const std::string hostStr = host.toString();
	DebugPrint("My host:port %s\n", hostStr.c_str());
#endif

	const auto ips = NetworkFildes.GetSocketAddresses();
	if (!ips.empty()) {
		DebugPrint("Num IP: %d\n", static_cast<int>(ips.size()));
		for (auto ip : ips) {
			DebugPrint("IP: %d.%d.%d.%d\n", NIPQUAD(ntohl(ip)));
		}
	} else {
		ErrorPrint("WARNING: Not connected to any external IPV4-network!\n");
		return;
	}
}

/**
**  Cleanup network.
*/
void ExitNetwork1()
{
	if (!IsNetworkGame()) { // No network running
		return;
	}

#ifdef DEBUG
	printStatistic(NetworkFildes.getStatistic());
	NetworkFildes.clearStatistic();
	NetworkStat.print();
#endif

	MeshGlue.reset();
	NetworkFildes.Close();
	NetExit(); // machine dependent setup

	NetworkInSync = true;
	NetPlayers = 0;
}

/**
**  Game will start now. (This function runs on all clients.)
*/
void NetworkOnStartGame()
{
	if (!IsNetworkGame()) {
		// really a single player game, but we use the command queue for determinism in replays
		CNetworkParameter::Instance.NetworkLag = 1;
	} else {
		CNetworkParameter::Instance.NetworkLag = 10;
	}
	ThisPlayer->SetName(Parameters::Instance.LocalPlayerName);
	for (int i = 0; i < NetPlayers; ++i) {
		Players[Hosts[i].PlyNr].SetName(Hosts[i].PlyName);
	}
	DebugPrint("Updates %d, Lag %d, Hosts %d\n",
	           CNetworkParameter::Instance.gameCyclesPerUpdate,
	           CNetworkParameter::Instance.NetworkLag,
	           NetPlayers);

	NetworkInSync = true;
	CommandsIn.clear();
	MsgCommandsIn.clear();
	// Prepare first time without syncs.
	for (int i = 0; i != 256; ++i) {
		for (int p = 0; p != PlayerMax; ++p) {
			for (int j = 0; j != MaxNetworkCommands; ++j) {
				NetworkIn[i][p][j].Clear();
			}
		}
	}
	CNetworkCommandSync nc;
	//nc.syncHash = SyncHash;
	//nc.syncSeed = SyncRandSeed;

	// push initial sync messages into command queues
	// timfel: why is this done on all clients and not just on the server?
	for (unsigned int i = 0; i <= CNetworkParameter::Instance.NetworkLag; i += CNetworkParameter::Instance.gameCyclesPerUpdate) {
		for (int n = 0; n < NetPlayers; ++n) {
			CNetworkCommandQueue(&ncqs)[MaxNetworkCommands] = NetworkIn[i][Hosts[n].PlyNr];

			ncqs[0].Time = i;
			ncqs[0].Type = MessageSync;
			ncqs[0].Data.resize(nc.Size());
			nc.Serialize(&ncqs[0].Data[0]);
			ncqs[1].Time = i;
			ncqs[1].Type = MessageNone;
		}
	}
	ranges::fill(NetworkSyncSeeds, 0);
	ranges::fill(NetworkSyncHashs, 0);
	ranges::fill(NetworkSyncCycles, 0);
	NetworkFirstDesyncGameCycle = 0;
	NetworkFirstDesyncSampleCycle = 0;
	NetworkGameInSync = true;
	ranges::fill(PlayerQuit, 0);
	ranges::fill(PlayerGone, 0);
	ranges::fill(PlayerQuitReason, 0);
	NetworkQuitAnnounced = false;
	ranges::fill(NetworkLastTick, 0);
	ranges::fill(NetworkLastCycle, 0);
	ResetSyncDebugCycleHistory();
	mesh::Params params;
	if (MeshDropSeconds > 0) {
		params.dropMs = MeshDropSeconds * 1000;
	}
	if (MeshLoadSeconds > 0) {
		params.loadMs = MeshLoadSeconds * 1000;
	}
	NetworkMeshStart(params);
	if (MeshGlue) {
		// Held (B.4): no cycle runs until every device has loaded.
		NetworkInSync = false;
	}
}

//----------------------------------------------------------------------------
//  Commands input
//----------------------------------------------------------------------------

/**
**  Prepare send of command message.
**
**  Convert arguments into network format and place it into output queue.
**
**  @param command  Command (Move,Attack,...).
**  @param unit     Unit that receive the command.
**  @param x        optional X map position.
**  @param y        optional y map position.
**  @param dest     optional destination unit.
**  @param type     optional unit-type argument.
**  @param flush    Append command or flush old commands.
**
**  @warning  Destination and unit-type shares the same network slot.
*/
void NetworkSendCommand(int command,
                        const CUnit &unit,
                        int x,
                        int y,
                        const CUnit *dest,
                        const CUnitType *type,
                        EFlushMode flush)
{
	if (NetworkIsHeld()) {
		return; // the game has not begun: nobody gives an order ahead of the others
	}
	CNetworkCommandQueue ncq;

	ncq.Time = GameCycle;
	ncq.Type = command;
	if (flush == EFlushMode::On) {
		ncq.Type |= 0x80;
	}
	CNetworkCommand nc;
	nc.Unit = UnitNumber(unit);
	nc.X = x;
	nc.Y = y;
	Assert(!dest || !type); // Both together isn't allowed
	if (dest) {
		nc.Dest = UnitNumber(*dest);
	} else if (type) {
		nc.Dest = type->Slot;
	} else {
		nc.Dest = 0xFFFF; // -1
	}
	ncq.Data.resize(nc.Size());
	nc.Serialize(&ncq.Data[0]);
	// Check for duplicate command in queue
	if (ranges::find(CommandsIn, ncq) != CommandsIn.end()) {
		return;
	}
	CommandsIn.push_back(ncq);
}

/**
**  Prepare send of extended command message.
**
**  Convert arguments into network format and place it into output queue.
**
**  @param command  Command (Move,Attack,...).
**  @param arg1     optional argument #1
**  @param arg2     optional argument #2
**  @param arg3     optional argument #3
**  @param arg4     optional argument #4
**  @param status   Append command or flush old commands.
*/
void NetworkSendExtendedCommand(int command, int arg1, int arg2, int arg3,
								int arg4, int status)
{
	if (NetworkIsHeld()) {
		return; // the game has not begun
	}
	CNetworkCommandQueue ncq;

	ncq.Time = GameCycle;
	ncq.Type = MessageExtendedCommand;
	if (status) {
		ncq.Type |= 0x80;
	}
	CNetworkExtendedCommand nec;
	nec.ExtendedType = command;
	nec.Arg1 = arg1;
	nec.Arg2 = arg2;
	nec.Arg3 = arg3;
	nec.Arg4 = arg4;
	ncq.Data.resize(nec.Size());
	nec.Serialize(&ncq.Data[0]);
	CommandsIn.push_back(ncq);
}

/**
**  Sends my selections to teammates
**
**  @param units  Units to send
**  @param count  Number of units to send
*/
void NetworkSendSelection(CUnit **units, int count)
{
	// Check if we have any teammates to send to
	const bool hasteammates = std::any_of(Hosts, Hosts + NetPlayers, [](const CNetworkHost &host) {
		return ThisPlayer->Index != host.PlyNr && host.IsValid()
		    && Players[host.PlyNr].Team == ThisPlayer->Team;
	});
	if (!hasteammates || NetworkIsHeld()) {
		return;
	}
	// Build and send packets to cover all units.
	CNetworkSelection ns;

	for (int i = 0; i != count; ++i) {
		ns.Units.push_back(UnitNumber(*units[i]));
	}
	CNetworkCommandQueue ncq;
	ncq.Time = GameCycle;
	ncq.Type = MessageSelection;

	ncq.Data.resize(ns.Size());
	ns.Serialize(&ncq.Data[0]);
	CommandsIn.push_back(ncq);
}

/**
**  Send chat message. (Message is sent with low priority)
**
**  @param msg  Text message to send.
*/
void NetworkSendChatMessage(const std::string &msg)
{
	if (!IsNetworkGame()) {
		return;
	}
	CNetworkChat nc;
	nc.Text = msg;
	CNetworkCommandQueue ncq;
	ncq.Type = MessageChat;
	ncq.Data.resize(nc.Size());
	nc.Serialize(&ncq.Data[0]);
	MsgCommandsIn.push_back(ncq);
}

/**
**  Remove a player from the game.
**
**  @param player  Player number
*/
static void NetworkRemovePlayer(int player)
{
	// Remove player from Hosts and clear NetworkIn
	if (auto it =
	        std::find_if(Hosts,
	                     Hosts + NetPlayers,
	                     [&](const auto &host) { return host.IsValid() && host.PlyNr == player; });
	    it != Hosts + NetPlayers) {
		it->Clear();
	}
	for (int i = 0; i < 256; ++i) {
		for (int c = 0; c < MaxNetworkCommands; ++c) {
			NetworkIn[i][player][c].Time = 0;
		}
	}
}

static bool IsNetworkCommandReady(int hostIndex, unsigned long gameNetCycle)
{
	if (!Hosts[hostIndex].IsValid()) {
		return true;
	}
	const int ply = Hosts[hostIndex].PlyNr;
	const CNetworkCommandQueue &ncq = NetworkIn[gameNetCycle & 0xFF][ply][0];

	if (ncq.Time != gameNetCycle) {
		return false;
	}
	return true;
}

static bool IsNetworkCommandReady(unsigned long gameNetCycle)
{
	// Check if all next messages are available.
	for (int i = 0; i < NetPlayers; ++i) {
		if (IsNetworkCommandReady(i, gameNetCycle) == false) {
			return false;
		}
	}

	return true;
}

/**
**  The next cycle may run. In a mesh game the model decides (it knows who is out and
**  who is frozen); the queue is asked as well, the two must never differ.
*/
static bool IsNetworkCycleReady(unsigned long gameNetCycle)
{
	if (MeshGlue && !MeshGlue->CanExecute(gameNetCycle)) {
		return false;
	}
	return IsNetworkCommandReady(gameNetCycle);
}

static void ParseResendCommand(const CNetworkPacket &packet, const CHost &host)
{
	if (!ThisPlayer) {
		DebugPrint("Ignoring resend command before local player is initialized\n");
		return;
	}
	// Destination cycle (time to execute).
	unsigned long n = ((GameCycle + 128) & ~0xFF) | packet.Header.Cycle;
	if (n > GameCycle + 128) {
		n -= 0x100;
	}
	const unsigned long gameNetCycle = n;
	// FIXME: not necessary to send this packet multiple times!!!!
	// other side sends re-send until it gets an answer.
	if (n != NetworkIn[gameNetCycle & 0xFF][ThisPlayer->Index][0].Time) {
		// Asking for a cycle we haven't gotten to yet, ignore for now
		return;
	}
	// Only the asker misses the packet.
	CNetworkPacket own;
	const int numcommands = NetworkBuildPacket(NetworkIn[gameNetCycle & 0xFF][ThisPlayer->Index], own);
	NetworkSendTo(host, own, numcommands);
	// Check if a player quit this cycle
	for (int j = 0; j < NetPlayers; ++j) {
		if (!Hosts[j].IsValid()) {
			continue;
		}
		for (int c = 0; c < MaxNetworkCommands; ++c) {
			const CNetworkCommandQueue *ncq;
			ncq = &NetworkIn[gameNetCycle & 0xFF][Hosts[j].PlyNr][c];
			if (ncq->Time && ncq->Type == MessageQuit) {
				CNetworkPacket np;
				np.Header.Cycle = ncq->Time & 0xFF;
				np.Header.OrigPlayer = Hosts[j].PlyNr; // the player who quit, not the sender
				np.Header.Type[0] = MessageQuit;
				np.Command[0] = ncq->Data;
				for (int k = 1; k < MaxNetworkCommands; ++k) {
					np.Header.Type[k] = MessageNone;
				}
				NetworkSendTo(host, np, 1);
				break;
			}
		}
	}
}

static bool IsAValidCommand_Command(const CNetworkPacket &packet, int index, const int player)
{
	CNetworkCommand nc;
	nc.Deserialize(&packet.Command[index][0]);
	const unsigned int slot = nc.Unit;
	const CUnit *unit = slot < UnitManager->GetUsedSlotCount() ? &UnitManager->GetSlotUnit(slot) : nullptr;

	// Nobody commands the units of a player who has quit.
	if (unit && !PlayerGone[unit->Player->Index]
		&& (unit->Player->Index == player
			|| Players[player].IsTeamed(*unit) || unit->Player->Type == PlayerTypes::PlayerNeutral)) {
		return true;
	} else {
		return false;
	}
}

static bool IsAValidCommand_Dismiss(const CNetworkPacket &packet, int index, const int player)
{
	CNetworkCommand nc;
	nc.Deserialize(&packet.Command[index][0]);
	const unsigned int slot = nc.Unit;
	const CUnit *unit = slot < UnitManager->GetUsedSlotCount() ? &UnitManager->GetSlotUnit(slot) : nullptr;

	if (unit && unit->Type->ClicksToExplode) {
		return true;
	}
	return IsAValidCommand_Command(packet, index, player);
}

static bool IsAValidCommand(const CNetworkPacket &packet, int index, const int player)
{
	switch (packet.Header.Type[index] & 0x7F) {
		case MessageExtendedCommand: // FIXME: ensure the sender is part of the command
		case MessageSync: // Sync does not matter
		case MessageSelection: // FIXME: ensure it's from the right player
		case MessageQuit:      // FIXME: ensure it's from the right player
		case MessageResend:    // FIXME: ensure it's from the right player
		case MessageChat:      // FIXME: ensure it's from the right player
			return true;
		case MessageCommandDismiss: return IsAValidCommand_Dismiss(packet, index, player);
		default: return IsAValidCommand_Command(packet, index, player);
	}
	// FIXME: not all values in nc have been validated
}

static void NetworkParseInGameEvent(const unsigned char *buf, int len, const CHost &host, bool relayed)
{
	if (!ThisPlayer) {
		DebugPrint("Ignoring in-game network event before local player is initialized\n");
		return;
	}
	CNetworkPacket packet;
	int commands;
	packet.Deserialize(buf, len, &commands);

	// A packet is taken only from the address a player is known at.
	int player = packet.Header.OrigPlayer;
	const int index = FindHostIndexBy(host);
	if (index == -1 || (player == 255 && PlayerQuit[Hosts[index].PlyNr])) {
#ifdef DEBUG
		const std::string hostStr = host.toString();
		DebugPrint("Not a host in play: %s\n", hostStr.c_str());
#endif
		return;
	}
	if (MeshGlue && !MeshGlue->Accepts(index)) {
		// Frozen by a removal round, or removed: nothing of his is taken; the model may answer.
		MeshGlue->OnHeard(index, NetworkTicks());
		return;
	}
	if (relayed) {
		// Passed on by another device: it must be the packet of a player of this game.
		if (!MeshGlue || player == ThisPlayer->Index || MeshGlue->SlotOfPlayer(player) == -1
			|| PlayerGone[player]) {
			return;
		}
	} else if (player == 255) {
		player = Hosts[index].PlyNr;
	} else if (player != Hosts[index].PlyNr
			   && (NetMesh || NetConnectType == 1 || index != 0 || player >= PlayerMax)) {
		// Only the server passes on the packets of other players, and in a mesh nobody does.
		DebugPrint("Player %d sent a packet of player %d\n", Hosts[index].PlyNr, player);
		return;
	}
	if (NetConnectType == 1 && !NetMesh) {
		if (player != 255) {
			NetworkBroadcast(packet, commands, player);
		}
	}
	if (commands < 0) {
		DebugPrint("Bad packet read\n");
		return;
	}
	if (MeshGlue) {
		// With the model a quit is the leaver's own, and a request is no content of a relay.
		for (int i = 0; i != commands; ++i) {
			if (packet.Header.Type[i] == MessageResend && relayed) {
				return;
			}
			if (packet.Header.Type[i] == MessageQuit) {
				CNetworkCommandQuit nc;
				nc.Deserialize(&packet.Command[i][0]);
				if (nc.player != player) {
					DebugPrint("Player %d sent the quit of player %d\n", player, nc.player);
					return;
				}
			}
		}
	}
	NetworkLastCycle[player] = packet.Header.Cycle;
	// Destination cycle (time to execute).
	unsigned long gameNetCycle = ((GameCycle + 128) & ~0xFF) | packet.Header.Cycle;
	if (gameNetCycle > GameCycle + 128) {
		gameNetCycle -= 0x100;
	}
	if (commands == 0 || packet.Header.Type[0] != MessageResend) {
		// The first packet of a player for a cycle wins, a later one changes nothing.
		// A packet for a cycle already executed comes too late.
		if (gameNetCycle <= GameCycle
			|| NetworkIn[packet.Header.Cycle][player][0].Time == gameNetCycle) {
			if (!relayed) {
				NetworkLastTick[player] = NetworkTicks(); // still a sign of life
			}
			NetworkMeshPacketSeen(index, player, gameNetCycle, relayed);
			return;
		}
	}
	// Parse the packet commands.
	for (int i = 0; i != commands; ++i) {
		// Handle some messages.
		if (packet.Header.Type[i] == MessageQuit) {
			CNetworkCommandQuit nc;
			nc.Deserialize(&packet.Command[i][0]);
			const int playerNum = nc.player;

			if (playerNum >= 0 && playerNum < NumPlayers) {
				PlayerQuit[playerNum] = 1;
			}
		}
		if (packet.Header.Type[i] == MessageResend) {
			ParseResendCommand(packet, host);
			if (MeshGlue) {
				MeshGlue->OnHeard(index, NetworkTicks());
			}
			return;
		}
		// Receive statistic
		if (!relayed) {
			NetworkLastTick[player] = NetworkTicks();
		}

		bool validCommand = IsAValidCommand(packet, i, player);
		// Place in network in
		if (validCommand) {
			NetworkIn[packet.Header.Cycle][player][i].Time = gameNetCycle;
			NetworkIn[packet.Header.Cycle][player][i].Type = packet.Header.Type[i];
			NetworkIn[packet.Header.Cycle][player][i].Data = packet.Command[i];
		} else {
			SetMessage(_("%s sent bad command"), Players[player].Name.c_str());
			DebugPrint("%s sent bad command: 0x%x\n",
			           Players[player].Name.c_str(),
			           packet.Header.Type[i] & 0x7F);
		}
	}
	for (int i = commands; i != MaxNetworkCommands; ++i) {
		NetworkIn[packet.Header.Cycle][player][i].Time = 0;
	}
	NetworkMeshPacketSeen(index, player, gameNetCycle, relayed);
	// Waiting for this time slot
	if (!NetworkInSync) {
		const int networkUpdates = CNetworkParameter::Instance.gameCyclesPerUpdate;
		unsigned long n = ((GameCycle / networkUpdates) + 1) * networkUpdates;
		if (IsNetworkCycleReady(n) == true) {
			NetworkInSync = true;
		}
	}
}

/**
**  Called if message for the network is ready.
**  (by WaitEventsOneFrame)
*/
void NetworkEvent()
{
	static constexpr size_t networkEventBufferSize = std::max(CInitMessage_State::Size(), static_cast<decltype(CInitMessage_State::Size())>(1024));

	if (!IsNetworkGame()) {
		NetworkInSync = true;
		return;
	}
	// Read the packet.
	std::array<unsigned char, networkEventBufferSize> buf;
	CHost host;
	int len = NetworkFildes.Recv(buf.data(), buf.size(), &host);
	if (len < 0) {
		DebugPrint("Server/Client gone?\n");
		// just hope for an automatic recover right now..
		NetworkInSync = false;
		return;
	}

	// Mesh datagrams, also while the room still runs (the probes)
	if (len > 0 && buf[0] == MessageMesh) {
		NetworkParseMeshEvent(buf.data(), len, host);
		return;
	}

	if (OnlineContextHandler->handleUDP(buf.data(), len, host)) {
		return;
	}

	// Setup messages
	if (NetConnectRunning) {
		if (NetworkParseSetupEvent(buf.data(), len, host)) {
			return;
		}
	}
	const unsigned char msgtype = buf[0];
	if (msgtype == MessageInit_FromClient || msgtype == MessageInit_FromServer) {
		return;
	}
	NetworkParseInGameEvent(buf.data(), len, host);
}

/**
**  Mesh game, the local player has left (B.6 step 2): stay until the others had their
**  time to ask for his last packets. The model answers their requests and says when
**  it is enough (no request for a while, or its cap). The quit itself is repeated
**  meanwhile, so that a device which lost it need not notice that first.
**
**  @param quit  The quit of the local player in his place of the queue.
*/
static void NetworkMeshLinger(const CNetworkCommandQueue(&quit)[MaxNetworkCommands])
{
	const unsigned long start = NetworkTicks();
	unsigned long lastSent = start;

	while (MeshGlue->Lingering() && NetworkTicks() - start < MeshLingerMaxMs) {
		if (NetworkFildes.HasDataToRead(MeshLingerPollMs) > 0) {
			std::array<unsigned char, 1024> buf;
			CHost host;
			const int len = NetworkFildes.Recv(buf.data(), buf.size(), &host);
			// The game is over here: only what the model answers is read.
			if (len > 0 && buf[0] == MessageMesh) {
				NetworkParseMeshEvent(buf.data(), len, host);
			}
		}
		const unsigned long now = NetworkTicks();
		if (now - lastSent >= MeshQuitRepeatMs) {
			lastSent = now;
			NetworkSendPacket(quit);
		}
		MeshGlue->Tick(now);
	}
}

/**
**  Quit the game: tell the others, once per game. Called while the cycle of the game
**  is still the one it stopped at.
*/
void NetworkQuitGame()
{
	if (!ThisPlayer || IsNetworkGame() == false || NetworkQuitAnnounced) {
		return;
	}
	NetworkQuitAnnounced = true;
	const int gameCyclesPerUpdate = CNetworkParameter::Instance.gameCyclesPerUpdate;
	const int NetworkLag = CNetworkParameter::Instance.NetworkLag;
	const int n = (GameCycle + gameCyclesPerUpdate) / gameCyclesPerUpdate * gameCyclesPerUpdate + NetworkLag;
	CNetworkCommandQueue(&ncqs)[MaxNetworkCommands] = NetworkIn[n & 0xFF][ThisPlayer->Index];
	CNetworkCommandQuit nc;
	nc.player = ThisPlayer->Index;
	nc.reason = NetQuitLeft;
	ncqs[0].Type = MessageQuit;
	ncqs[0].Time = n;
	ncqs[0].Data.resize(nc.Size());
	nc.Serialize(&ncqs[0].Data[0]);
	for (int i = 1; i < MaxNetworkCommands; ++i) {
		ncqs[i].Type = MessageNone;
		ncqs[i].Data.clear();
	}
	NetworkSendPacket(ncqs);
	if (MeshGlue) {
		MeshGlue->OnQuit(NetworkTicks());
		NetworkMeshLinger(ncqs);
		// This device takes no part any more; a game which loads next has no model yet.
		MeshGlue.reset();
	}
}

static void NetworkExecCommand_Sync(const CNetworkCommandQueue &ncq, int player)
{
	Assert((ncq.Type & 0x7F) == MessageSync);

	CNetworkCommandSync nc;
	nc.Deserialize(&ncq.Data[0]);
	const unsigned long gameNetCycle = GameCycle;
	const unsigned int syncSeed = nc.syncSeed;
	const unsigned int syncHash = nc.syncHash;
	const unsigned int localSeed = NetworkSyncSeeds[gameNetCycle & 0xFF];
	const unsigned int localHash = NetworkSyncHashs[gameNetCycle & 0xFF];
	const unsigned long sampledCycle = NetworkSyncCycles[gameNetCycle & 0xFF];

	if (syncSeed != localSeed || syncHash != localHash) {
		// if it wasn't already, force enable debug output right now. maybe we get lucky ...
		EnableDebugPrint = true;
		EnableUnitDebug = true;
		if (NetworkGameInSync || (gameNetCycle % (CYCLES_PER_SECOND * 5)) == 0) {
			// only print this message circa every 5 seconds...
			SetMessage("%s", _("Network out of sync"));
			NetworkGameInSync = false;
			SetGamePaused(true);

			time_t now;
			time(&now);
			std::string savefile = "desync_savegame_";
			savefile += std::to_string(ThisPlayer->Index);
			savefile += "_";
			savefile += std::to_string((intmax_t)now);
			savefile += ".sav";
			SaveGame(savefile);
		}
		if (!NetworkFirstDesyncGameCycle) {
			NetworkFirstDesyncGameCycle = gameNetCycle;
			NetworkFirstDesyncSampleCycle = sampledCycle;
			ErrorPrint("\nFirst network desync detected from player %d (%s): "
			           "exec-cycle %lu, sampled-after-cycle %lu, packet-time %lu, "
			           "network-lag %u, update-rate %u\n",
			           player,
			           Players[player].Name.c_str(),
			           gameNetCycle,
			           sampledCycle,
			           ncq.Time,
			           CNetworkParameter::Instance.NetworkLag,
			           CNetworkParameter::Instance.gameCyclesPerUpdate);
			DumpSyncDebugCycleHistory(sampledCycle, syncSeed, localSeed, syncHash, localHash);
		}
		ErrorPrint("\nNetwork out of sync seed: %X!=%X , hash: %X!=%X "
		           "Cycle %lu sampled-after-cycle %lu first-desync-cycle %lu "
		           "first-sampled-after-cycle %lu player %d (%s)\n\n",
		           syncSeed,
		           localSeed,
		           syncHash,
		           localHash,
		           GameCycle,
		           sampledCycle,
		           NetworkFirstDesyncGameCycle,
		           NetworkFirstDesyncSampleCycle,
		           player,
		           Players[player].Name.c_str());
	} else {
		NetworkGameInSync = true;
	}
}

static void NetworkExecCommand_Selection(const CNetworkCommandQueue &ncq)
{
	Assert((ncq.Type & 0x7F) == MessageSelection);

	CNetworkSelection ns;

	ns.Deserialize(&ncq.Data[0]);
	if (Players[ns.player].Team != ThisPlayer->Team) {
		return;
	}
	std::vector<CUnit *> units;

	for (auto unitId : ns.Units) {
		units.push_back(&UnitManager->GetSlotUnit(unitId));
	}
	ChangeTeamSelectedUnits(Players[ns.player], units);
}

static void NetworkExecCommand_Chat(const CNetworkCommandQueue &ncq)
{
	Assert((ncq.Type & 0x7F) == MessageChat);

	CNetworkChat nc;
	nc.Deserialize(&ncq.Data[0]);

	SetMessage("%s", nc.Text.c_str());
	PlayGameSound(GameSounds.ChatMessage.Sound.get(), MaxSampleVolume);
	CommandLog("chat", nullptr, EFlushMode::On, -1, -1, nullptr, nc.Text.c_str(), -1);
}

/**
**  The quit of a player is executed: he is out for good.
**
**  The one place a quit of the command stream ends in. Also called by the debug quit of the
**  debug bridge, which has no stream.
**
**  @param player  Player number.
**  @param reason  ENetQuitReason.
*/
void NetworkExecQuit(int player, int reason)
{
	NetworkRemovePlayer(player);
	PlayerGone[player] = 1;
	PlayerQuitReason[player] = reason;
	CommandLog("quit", nullptr, EFlushMode::On, player, -1, nullptr, nullptr, -1);
	CommandQuit(player, reason);
}

static void NetworkExecCommand_Quit(const CNetworkCommandQueue &ncq)
{
	Assert((ncq.Type & 0x7F) == MessageQuit);
	CNetworkCommandQuit nc;

	nc.Deserialize(&ncq.Data[0]);
	const int reason = nc.reason < NetQuitReasonCount ? nc.reason : NetQuitLeft;
	NetworkExecQuit(nc.player, reason);
}

static void NetworkExecCommand_ExtendedCommand(const CNetworkCommandQueue &ncq)
{
	Assert((ncq.Type & 0x7F) == MessageExtendedCommand);
	CNetworkExtendedCommand nec;

	nec.Deserialize(&ncq.Data[0]);
	ExecExtendedCommand(nec.ExtendedType, (ncq.Type & 0x80) >> 7,
						nec.Arg1, nec.Arg2, nec.Arg3, nec.Arg4);
}

static void NetworkExecCommand_Command(const CNetworkCommandQueue &ncq)
{
	CNetworkCommand nc;

	nc.Deserialize(&ncq.Data[0]);
	ExecCommand(ncq.Type, nc.Unit, nc.X, nc.Y, nc.Dest);
}

/**
**  Execute a network command.
**
**  @param ncq  Network command from queue
*/
static void NetworkExecCommand(const CNetworkCommandQueue &ncq, int player)
{
	switch (ncq.Type & 0x7F) {
		case MessageSync: NetworkExecCommand_Sync(ncq, player); break;
		case MessageSelection: NetworkExecCommand_Selection(ncq); break;
		case MessageChat: NetworkExecCommand_Chat(ncq); break;
		case MessageQuit: NetworkExecCommand_Quit(ncq); break;
		case MessageExtendedCommand: NetworkExecCommand_ExtendedCommand(ncq); break;
		case MessageNone:
			// Nothing to Do, This Message Should Never be Executed
			Assert(0);
			break;
		default: NetworkExecCommand_Command(ncq); break;
	}
}

/**
**  Network send commands.
*/
static void NetworkSendCommands(unsigned long gameNetCycle)
{
	// No command available, send sync.
	int numcommands = 0;
	CNetworkCommandQueue(&ncq)[MaxNetworkCommands] = NetworkIn[gameNetCycle & 0xFF][ThisPlayer->Index];
	ncq[0].Clear();
	if (CommandsIn.empty() && MsgCommandsIn.empty()) {
		CNetworkCommandSync nc;
		ncq[0].Type = MessageSync;
		nc.syncHash = SyncHash;
		nc.syncSeed = SyncRandSeed;
		ncq[0].Data.resize(nc.Size());
		nc.Serialize(&ncq[0].Data[0]);
		ncq[0].Time = gameNetCycle;
		numcommands = 1;
	} else {
		while (!CommandsIn.empty() && numcommands < MaxNetworkCommands) {
			const CNetworkCommandQueue &incommand = CommandsIn.front();
#ifdef DEBUG
			if (incommand.Type != MessageExtendedCommand) {
				CNetworkCommand nc;
				nc.Deserialize(&incommand.Data[0]);

				const CUnit &unit = UnitManager->GetSlotUnit(nc.Unit);
				// FIXME: we can send destroyed units over network :(
				if (unit.Destroyed) {
					DebugPrint("Sending destroyed unit %d over network!!!!!!\n", nc.Unit);
				}
			}
#endif
			ncq[numcommands] = incommand;
			ncq[numcommands].Time = gameNetCycle;
			++numcommands;
			CommandsIn.pop_front();
		}
		while (!MsgCommandsIn.empty() && numcommands < MaxNetworkCommands) {
			const CNetworkCommandQueue &incommand = MsgCommandsIn.front();
			ncq[numcommands] = incommand;
			ncq[numcommands].Time = gameNetCycle;
			++numcommands;
			MsgCommandsIn.pop_front();
		}
	}
	if (numcommands != MaxNetworkCommands) {
		ncq[numcommands].Type = MessageNone;
	}
	NetworkSyncSeeds[gameNetCycle & 0xFF] = SyncRandSeed;
	NetworkSyncHashs[gameNetCycle & 0xFF] = SyncHash;
	NetworkSyncCycles[gameNetCycle & 0xFF] = GameCycle > 0 ? GameCycle - 1 : 0;
	if (IsNetworkGame()) {
		NetworkSendPacket(ncq);
	}
}

/**
**  Network execute commands.
*/
static void NetworkExecCommands(unsigned long gameNetCycle)
{
	// Must execute commands on all computers in the same order.
	for (int i = 0; i < NumPlayers; ++i) {
		const CNetworkCommandQueue *ncqs = NetworkIn[gameNetCycle & 0xFF][i];
		for (int c = 0; c < MaxNetworkCommands; ++c) {
			const CNetworkCommandQueue &ncq = ncqs[c];
			if (ncq.Type == MessageNone) {
				break;
			}
			if (ncq.Time && ncq.Time == gameNetCycle) {
				NetworkExecCommand(ncq, i);
			}
		}
	}
}

/**
**  Handle network commands.
*/
void NetworkCommands()
{
	if ((GameCycle % CNetworkParameter::Instance.gameCyclesPerUpdate) != 0) {
		return;
	}
	const unsigned long gameNetCycle = GameCycle;
	// Send messages to all clients (other players)
	NetworkSendCommands(gameNetCycle + CNetworkParameter::Instance.NetworkLag);
	NetworkExecCommands(gameNetCycle);
	if (MeshGlue) {
		const unsigned long now = NetworkTicks();
		MeshGlue->OnExecuted(gameNetCycle, now);
		if (NetworkPauseAsked != -1) {
			// The pause begins after this cycle; the test below then holds the next one.
			MeshGlue->OnPause(NetworkPauseAsked, now);
		}
		MeshGlue->Tick(now);
	}
	NetworkPauseAsked = -1;
	NetworkInSync = IsNetworkCycleReady(gameNetCycle + CNetworkParameter::Instance.gameCyclesPerUpdate);
}

/**
**  A player timed out: place his quit in the next cycle and tell the others.
**
**  @param playerIndex  Player number
*/
static void NetworkTimeOutPlayer(int playerIndex)
{
	const unsigned int nextGameNetCycle = GameCycle / CNetworkParameter::Instance.gameCyclesPerUpdate + 1;
	CNetworkCommandQuit nc;
	nc.player = playerIndex;
	CNetworkCommandQueue *ncq = &NetworkIn[nextGameNetCycle & 0xFF][playerIndex][0];
	ncq->Time = nextGameNetCycle * CNetworkParameter::Instance.gameCyclesPerUpdate;
	ncq->Type = MessageQuit;
	ncq->Data.resize(nc.Size());
	nc.Serialize(&ncq->Data[0]);
	PlayerQuit[playerIndex] = 1;

	// The packet names the player it is about and carries his quit.
	CNetworkPacket np;
	np.Header.Cycle = ncq->Time & 0xFF;
	np.Header.OrigPlayer = playerIndex;
	np.Header.Type[0] = ncq->Type;
	np.Header.Type[1] = MessageNone;
	np.Command[0] = ncq->Data;

	NetworkBroadcast(np, 1, playerIndex);
}

static void CheckPlayerThatTimeOut(int hostIndex)
{
	if (!Hosts[hostIndex].IsValid()) {
		return;
	}
	const int playerIndex = Hosts[hostIndex].PlyNr;
	const unsigned long lastTick = NetworkLastTick[playerIndex];
	if (!lastTick) {
		return;
	}
	// Real time: a count of frames runs at the speed of the display.
	const int secs = (NetworkTicks() - lastTick) / 1000;
	// FIXME: display a menu while we wait
	const int timeoutInS = CNetworkParameter::Instance.timeoutInS;
	if (3 <= secs && secs < timeoutInS && FrameCounter % CyclesPerSecond == 0) {
		SetMessage(_("Waiting for player \"%s\": %d:%02d"), Hosts[hostIndex].PlyName,
				   (timeoutInS - secs) / 60, (timeoutInS - secs) % 60);
	}
	if (secs >= timeoutInS) {
		NetworkTimeOutPlayer(playerIndex);
		SetMessage("%s", _("Timed out"));
	}
}

/**
**  Network resend commands, we have a missing packet send to all clients
**  what packet we are missing.
**
**  @todo
**  We need only send to the clients, that have not delivered the packet.
*/
static void NetworkResendCommands()
{
#ifdef DEBUG
	++NetworkStat.resentPacketCount;
#endif

	const int networkUpdates = CNetworkParameter::Instance.gameCyclesPerUpdate;
	const int nextGameCycle = ((GameCycle / networkUpdates) + 1) * networkUpdates;
	// Build packet
	CNetworkPacket packet;
	packet.Header.Type[0] = MessageResend;
	packet.Header.Type[1] = MessageNone;
	packet.Header.Cycle = uint8_t(nextGameCycle & 0xFF);

	NetworkBroadcast(packet, 1);
}

/**
**  Mesh game: let the model ask for what is missing, wait for the silent and remove
**  the lost, then ask it whether the next cycle may run.
*/
static void NetworkMeshUpdate()
{
	MeshGlue->Tick(NetworkTicks());
	NetworkInSync = IsNetworkCycleReady(GameCycle + 1);
	if (MeshGlue->Paused()) {
		GamePaused = true; // a window that closes lets the game run again; not this pause
	}
}

/**
**  Mesh game: the text of the wait, once a second.
*/
static void NetworkMeshShowWaiting()
{
	if (NetworkWaitScripted()) {
		// The scripts show the wait themselves (Lua NetWaitFrame).
		return;
	}
	if (FrameCounter % CyclesPerSecond != 0) {
		return;
	}
	const CNetworkWaitState state = NetworkGetWaitState();
	// FIXME: display a menu while we wait
	if (state.SelfOut) {
		SetMessage("%s", _("Connection to the game lost"));
	} else if (state.Player != -1 && state.Removing) {
		SetMessage(_("Removing player \"%s\""), Players[state.Player].Name.c_str());
	} else if (state.Player != -1) {
		const int secs = (state.MsLeft + 999) / 1000;
		SetMessage(_("Waiting for player \"%s\": %d:%02d"), Players[state.Player].Name.c_str(),
				   secs / 60, secs % 60);
	}
}

/**
**  Recover network.
*/
void NetworkRecover()
{
	if (NetPlayers == 0) {
		NetworkInSync = true;
		return;
	}
	if (FrameCounter % CNetworkParameter::Instance.gameCyclesPerUpdate != 0) {
		return;
	}
	if (MeshGlue) {
		// No time-out by the own clock and no quit made up here: the devices agree on it.
		NetworkMeshUpdate();
		NetworkMeshShowWaiting();
		return;
	}
	for (int i = 0; i < NetPlayers; ++i) {
		if (ThisPlayer->Index == Hosts[i].PlyNr) {
			continue; // skip self
		}
		CheckPlayerThatTimeOut(i);
	}
	NetworkResendCommands();
	const unsigned int nextGameNetCycle = GameCycle / CNetworkParameter::Instance.gameCyclesPerUpdate + 1;
	NetworkInSync = IsNetworkCommandReady(nextGameNetCycle);
}

//----------------------------------------------------------------------------
//  Test hooks
//----------------------------------------------------------------------------

/**
**  Parse an in-game packet as if it had been received from host.
**
**  @param buf   Packet.
**  @param len   Size of the packet.
**  @param host  Host the packet comes from.
*/
void NetworkTestParseInGameEvent(const unsigned char *buf, int len, const CHost &host)
{
	NetworkParseInGameEvent(buf, len, host);
}

/**
**  Get the stored command of a player for a cycle.
**
**  @param cycle   Cycle to execute the command.
**  @param player  Player number.
**  @param index   Index of the command in the packet.
**  @param type    Out: command type.
**  @param data    Out: command content (network format).
**
**  @return  false if the store holds no such command of that cycle.
*/
bool NetworkTestGetCommand(unsigned long cycle, int player, int index,
                           unsigned char &type, std::vector<unsigned char> &data)
{
	const CNetworkCommandQueue &ncq = NetworkIn[cycle & 0xFF][player][index];

	if (ncq.Time != cycle) {
		return false;
	}
	type = ncq.Type;
	data = ncq.Data;
	return true;
}

/**
**  Time out a player now, without the wait and without the message on the screen.
**
**  @param player  Player number.
*/
void NetworkTestTimeOutPlayer(int player)
{
	NetworkTimeOutPlayer(player);
}

/**
**  Player is marked as quit.
**
**  @param player  Player number.
*/
bool NetworkTestPlayerQuit(int player)
{
	return PlayerQuit[player] != 0;
}

/**
**  Handle a mesh datagram as if it had been received from host.
**
**  @param buf   Datagram.
**  @param len   Size of the datagram.
**  @param host  Host the datagram comes from.
*/
void NetworkTestParseMeshEvent(const unsigned char *buf, int len, const CHost &host)
{
	NetworkParseMeshEvent(buf, len, host);
}

/**
**  Take the mesh messages accepted since the last call.
**
**  @return  Host slot of the sender and message, oldest first.
*/
std::vector<std::pair<int, mesh::Msg>> NetworkTestTakeMeshMessages()
{
	std::vector<std::pair<int, mesh::Msg>> res(NetworkMeshReceived.begin(), NetworkMeshReceived.end());

	NetworkMeshReceived.clear();
	return res;
}

/**
**  Number of mesh datagrams dropped.
*/
unsigned int NetworkTestMeshDropped()
{
	return NetworkMeshDropped;
}

/**
**  Start the model of a mesh game with the given time bounds (NetMesh must be on).
**
**  @return  false if the game is none the model counts with.
*/
bool NetworkTestMeshStart(const mesh::Params &params)
{
	NetworkMeshStart(params);
	return MeshGlue != nullptr;
}

/**
**  End the model of the mesh game.
*/
void NetworkTestMeshStop()
{
	MeshGlue.reset();
}

/**
**  What NetworkRecover does in a mesh game, without the text on the screen.
*/
void NetworkTestMeshUpdate()
{
	if (MeshGlue) {
		NetworkMeshUpdate();
	}
}

/**
**  The model of the running mesh game, null if there is none.
*/
const CMeshGlue *NetworkTestMeshGlue()
{
	return MeshGlue.get();
}

//@}
