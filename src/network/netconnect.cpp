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
/**@name netconnect.cpp - The network high level connection code. */
//
//      (c) Copyright 2001-2013 by Lutz Sammer, Andreas Arens, and Jimmy Salmon
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
// Includes
//----------------------------------------------------------------------------

#include <iostream>
#include <fstream>
#include <set>
#include <vector>
#include <functional>
#include <algorithm>
#include <array>
#include <utility>
#include <random>

#include "filesystem.h"
#include "game.h"
#include "online_service.h"
#include "stratagus.h"

#include "netconnect.h"

#include "interface.h"
#include "map.h"
#include "iolib.h"
#include "mdns_wrapper.h"
#include "net_lowlevel.h"
#include "net_mesh.h"
#include "net_mesh_wire.h"
#include "network.h"
#include "network_test_hooks.h"
#include "parameters.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "version.h"
#include "video.h"

#include <SDL.h>

//----------------------------------------------------------------------------
// Declaration
//----------------------------------------------------------------------------

// received nothing from client for xx frames?
#define CLIENT_LIVE_BEAT 60
#define CLIENT_IS_DEAD 300

// received nothing from client for xx ms while the game is started?
#define CLIENT_START_TIMEOUT 15000

// the clients probe each other for at most xx ms before the server decides against the mesh
#define MESH_PROBE_TIMEOUT 2000
// a client repeats its probes every xx ms
#define MESH_PROBE_INTERVAL 100

/**
**  Connect state information of network systems active in current game.
*/
struct NetworkState {
	void Clear()
	{
		State = ccs_unused;
		MsgCnt = 0;
		StateArg = 0;
		LastFrame = 0;
	}

	unsigned char State;     /// Menu: ConnectState
	unsigned short MsgCnt;   /// Menu: Counter for state msg of same type (detect unreachable)
	unsigned int StateArg;   /// Arbitrary argument for state to keep track of things
	unsigned long LastFrame; /// Last message received
	// Fill in here...
};

//----------------------------------------------------------------------------
// Variables
//----------------------------------------------------------------------------

CNetworkHost Hosts[PlayerMax];         /// Host, ports, and assigned player numbers of all players.

int NetConnectRunning = 0;             /// Network menu: Setup mode active
int NetConnectType = 0;             /// Network menu: Setup mode active
int NetLocalHostsSlot;                 /// Network menu: Slot # in Hosts array of local client
int NetLocalPlayerNumber;              /// Player number of local client
bool NetMesh = false;                  /// This game: every device sends to every device, nobody forwards

static bool NetMeshWanted = false;     /// Server: ask for the mesh at the start of a game
static uint16_t NetMeshProbeMask;      /// Client: bit n = the peer in host slot n answered our probe
static std::vector<std::pair<int, int>> NetMeshProbeFailed; /// Server: (a, b) = slot a got no answer from slot b

int NetPlayers;                         /// How many network players
std::string NetworkMapName;             /// Name of the map received with ICMMap
std::string NetworkMapFragmentName;     /// Name of the map currently loading via ICMMapNeeded
bool NoRandomPlacementMultiplayer = false; /// Disable the random placement of players in muliplayer mode

CServerSetup ServerSetupState; // Server selection state for Multiplayer clients
CServerSetup LocalSetupState;  // Local selection state for Multiplayer clients

MDNS MdnsService; // Service discovery for open LAN games

class CServer
{
public:
	void Init(const std::string &name, CUDPSocket *socket, CServerSetup *serverSetup);

	void Update(unsigned long frameCounter);
	void Parse(unsigned long frameCounter, const unsigned char *buf, const CHost &host);

	void MarkClientsAsResync();
	bool AllClientsSynced() const;
	void KickClient(int c);
private:
	int Parse_Hello(int h, const CInitMessage_Hello &msg, const CHost &host);
	void Parse_Resync(const int h);
	void Parse_Waiting(const int h);
	void Parse_Map(const int h);
	void Parse_MapFragment(const int h, uint32_t fragmentIdx);
	void Parse_State(const int h, const CInitMessage_State &msg);
	void Parse_GoodBye(const int h);
	void Parse_SeeYou(const int h);

	void Send_AreYouThere(const CNetworkHost &host);
	void Send_GameFull(const CHost &host);
	void Send_Welcome(const CNetworkHost &host, int hostIndex);
	void Send_Resync(const CNetworkHost &host, int hostIndex);
	void Send_Map(const CNetworkHost &host);
	void Send_MapFragment(const CNetworkHost &host, uint32_t fragmentIdx);
	void Send_State(const CNetworkHost &host);
	void Send_GoodBye(const CNetworkHost &host);
private:
	std::string name;
	NetworkState networkStates[PlayerMax]; /// Client Host states
	CUDPSocket *socket;
	CServerSetup *serverSetup;
};

class CClient
{
public:
	void Init(const std::string &name, CUDPSocket *socket, CServerSetup *serverSetup, CServerSetup *localSetup, unsigned long tick);
	void SetServerHost(const CHost &host) { serverHost = host; }

	bool Parse(const unsigned char *buf, const CHost &host);
	bool Update(unsigned long tick);

	void DetachFromServer();

	int GetNetworkState() const { return networkState.State; }
	void TestSetState(int state) { networkState.State = state; networkState.MsgCnt = 0; }

private:
	bool Update_disconnected();
	bool Update_detaching(unsigned long tick);
	bool Update_connecting(unsigned long tick);
	bool Update_connected(unsigned long tick);
	bool Update_synced(unsigned long tick);
	bool Update_changed(unsigned long tick);
	bool Update_async(unsigned long tick);
	bool Update_mapinfo(unsigned long tick);
	bool Update_badmap(unsigned long tick);
	bool Update_needmap(unsigned long tick);
	bool Update_goahead(unsigned long tick);
	bool Update_started(unsigned long tick);

	void Send_Go(unsigned long tick);
	void Send_Config(unsigned long tick);
	void Send_MapUidMismatch(unsigned long tick);
	void Send_MapNeeded(unsigned long tick, unsigned int fragmentIdx, bool limit = true);
	void Send_Map(unsigned long tick);
	void Send_Resync(unsigned long tick);
	void Send_State(unsigned long tick);
	void Send_Waiting(unsigned long tick, unsigned long msec);
	void Send_Hello(unsigned long tick);
	void Send_GoodBye(unsigned long tick);
	void Send_Probes(unsigned long tick);

	template <typename T>
	void SendRateLimited(const T &msg, unsigned long tick, unsigned long msecs);

	void SetConfig(const CInitMessage_Config &msg);

	void Parse_GameFull();
	void Parse_LuaMismatch(const unsigned char *buf);
	void Parse_EngineMismatch(const unsigned char *buf);
	void Parse_Resync(const unsigned char *buf);
	void Parse_Config(const unsigned char *buf);
	void Parse_State(const unsigned char *buf);
	void Parse_Welcome(const unsigned char *buf);
	void Parse_Map(const unsigned char *buf);
	void Parse_MapFragment(const unsigned char *buf);
	void Parse_AreYouThere();

private:
	std::string name;
	CHost serverHost;  /// IP:port of server to join
	NetworkState networkState;
	unsigned char lastMsgTypeSent;  /// Subtype of last InitConfig message sent
	CUDPSocket *socket;
	CServerSetup *serverSetup;
	CServerSetup *localSetup;
	bool meshProbe = false;         /// The Config asked for the mesh: probe the other clients
	unsigned long lastProbeTick = 0; /// Tick (ms) of the last probes, 0 = none sent
};

static CServer Server;
static CClient Client;

//
// CClient
//

/**
** Send an InitConfig message across the Network
**
** @param host Host to send to (network byte order).
** @param port Port of host to send to (network byte order).
** @param msg The message to send
*/
template <typename T>
static void NetworkSendICMessage(CUDPSocket &socket, const CHost &host, const T &msg)
{
	std::vector<unsigned char> buf = msg.Serialize();
	socket.Send(host, buf.data(), buf.size());
}

void NetworkSendICMessage(CUDPSocket &socket, const CHost &host, const CInitMessage_Header &msg)
{
	std::vector<unsigned char> buf(msg.Size());
	msg.Serialize(buf.data());
	socket.Send(host, buf.data(), buf.size());
}

static const char *ncconstatenames[] = {
	"ccs_unused",
	"ccs_connecting",          // new client
	"ccs_connected",           // has received slot info
	"ccs_mapinfo",             // has received matching map-info
	"ccs_badmap",              // has received non-matching map-info
	"ccs_synced",              // client is in sync with server
	"ccs_async",               // server user has changed selection
	"ccs_changed",             // client user has made menu selection
	"ccs_detaching",           // client user wants to detach
	"ccs_disconnected",        // client has detached
	"ccs_unreachable",         // server is unreachable
	"ccs_usercanceled",        // user canceled game
	"ccs_nofreeslots",         // server has no more free slots
	"ccs_serverquits",         // server quits
	"ccs_goahead",             // server wants to start game
	"ccs_started",             // server has started game
	"ccs_incompatibleengine",  // incompatible engine version
	"ccs_incompatibleluafiles", // incompatible network version
	"ccs_needmap",             // client needs to be sent the map
};

static const char *icmsgsubtypenames[] = {
	"Hello",                   // Client Request
	"Config",                  // Setup message configure clients

	"EngineMismatch",          // Stratagus engine version doesn't match
	"ProtocolMismatch",        // Network protocol version doesn't match
	"EngineConfMismatch",      // Engine configuration isn't identical
	"MapUidMismatch",          // MAP UID doesn't match

	"GameFull",                // No player slots available
	"Welcome",                 // Acknowledge for new client connections

	"Waiting",                 // Client has received Welcome and is waiting for Map/State
	"Map",                     // MapInfo (and Mapinfo Ack)
	"State",                   // StateInfo
	"Resync",                  // Ack StateInfo change

	"ServerQuit",              // Server has quit game
	"GoodBye",                 // Client wants to leave game
	"SeeYou",                  // Client has left game

	"Go",                      // Client is ready to run
	"AreYouThere",             // Server asks are you there
	"IAmHere",                 // Client answers I am here

	"MapNeeded",               // Map data exchange
};

template <typename T>
static void NetworkSendICMessage_Log(CUDPSocket &socket, const CHost &host, const T &msg)
{
	NetworkSendICMessage(socket, host, msg);

#ifdef DEBUG
	const std::string hostStr = host.toString();
	DebugPrint(
		"Sending to %s -> %s\n", hostStr.c_str(), icmsgsubtypenames[msg.GetHeader().GetSubType()]);
#endif
}

static void NetworkSendICMessage_Log(CUDPSocket &socket, const CHost &host, const CInitMessage_Header &msg)
{
	NetworkSendICMessage(socket, host, msg);

#ifdef DEBUG
	const std::string hostStr = host.toString();
	DebugPrint("Sending to %s -> %s\n", hostStr.c_str(), icmsgsubtypenames[msg.GetSubType()]);
#endif
}

/**
** Send a message to the server, but only if the last packet was a while ago
**
** @param msg    The message to send
** @param tick   current tick
** @param msecs  microseconds to delay
*/
template <typename T>
void CClient::SendRateLimited(const T &msg, unsigned long tick, unsigned long msecs)
{
	const unsigned long now = tick;
	if (now - networkState.LastFrame < msecs) {
		return;
	}
	networkState.LastFrame = now;
	const unsigned char subtype = msg.GetHeader().GetSubType();
	if (subtype == lastMsgTypeSent) {
		++networkState.MsgCnt;
	} else {
		networkState.MsgCnt = 0;
		lastMsgTypeSent = subtype;
	}
	NetworkSendICMessage(*socket, serverHost, msg);
	DebugPrint("[%s] Sending (%s:#%d)\n",
	           ncconstatenames[networkState.State],
	           icmsgsubtypenames[subtype],
	           networkState.MsgCnt);
}

template<>
void CClient::SendRateLimited<CInitMessage_Header>(const CInitMessage_Header &msg, unsigned long tick, unsigned long msecs)
{
	const unsigned long now = tick;
	if (now - networkState.LastFrame < msecs) {
		return;
	}
	networkState.LastFrame = now;
	const unsigned char subtype = msg.GetSubType();
	if (subtype == lastMsgTypeSent) {
		++networkState.MsgCnt;
	} else {
		networkState.MsgCnt = 0;
		lastMsgTypeSent = subtype;
	}
	NetworkSendICMessage(*socket, serverHost, msg);
	DebugPrint("[%s] Sending (%s:#%d)\n",
	           ncconstatenames[networkState.State],
	           icmsgsubtypenames[subtype],
	           networkState.MsgCnt);
}

void CClient::Init(const std::string &name, CUDPSocket *socket, CServerSetup *serverSetup, CServerSetup *localSetup, unsigned long tick)
{
	networkState.LastFrame = tick;
	networkState.State = ccs_connecting;
	networkState.MsgCnt = 0;
	lastMsgTypeSent = ICMServerQuit;
	this->serverSetup = serverSetup;
	this->localSetup = localSetup;
	this->name = name;
	this->socket = socket;
	meshProbe = false;
	lastProbeTick = 0;
}

void CClient::DetachFromServer()
{
	networkState.State = ccs_detaching;
	networkState.MsgCnt = 0;
}

bool CClient::Update_disconnected()
{
	Assert(networkState.State == ccs_disconnected);
	const CInitMessage_Header message(MessageInit_FromClient, ICMSeeYou);

	// Spew out 5 and trust in God that they arrive
	for (int i = 0; i < 5; ++i) {
		NetworkSendICMessage(*socket, serverHost, message);
	}
	networkState.State = ccs_usercanceled;
	return false;
}

bool CClient::Update_detaching(unsigned long tick)
{
	Assert(networkState.State == ccs_detaching);

	if (networkState.MsgCnt < 10) { // 10 retries = 1 second
		Send_GoodBye(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_detaching: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_connecting(unsigned long tick)
{
	Assert(networkState.State == ccs_connecting);

	if (networkState.MsgCnt < 48) { // 48 retries = 24 seconds
		Send_Hello(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_connecting: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_connected(unsigned long tick)
{
	Assert(networkState.State == ccs_connected);

	if (networkState.MsgCnt < 20) { // 20 retries
		Send_Waiting(tick, 650);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_connected: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

int NetworkGetPlayerIndexForHost(int hostIndex)
{
	if (hostIndex < 0 || hostIndex >= PlayerMax || !Hosts[hostIndex].IsValid()) {
		return -1;
	}
	if (Hosts[hostIndex].PlyNr >= PlayerMax) {
		return -1;
	}
	return Hosts[hostIndex].PlyNr;
}

bool NetworkIsLocalSetupInSync(const CServerSetup &state1, const CServerSetup &state2, int hostIndex)
{
	if (hostIndex < 0 || hostIndex >= PlayerMax) {
		return true;
	}
	const int playerIndex = NetworkGetPlayerIndexForHost(hostIndex);
	if (playerIndex == -1) {
		return state1.Ready[hostIndex] == state2.Ready[hostIndex];
	}
	return (state1.ServerGameSettings.Presets[playerIndex].Race
	            == state2.ServerGameSettings.Presets[playerIndex].Race
	        && state1.Ready[hostIndex] == state2.Ready[hostIndex]);
}

void NetworkApplyClientSetupStateChange(CServerSetup &serverSetup,
                                        const CServerSetup &clientSetup,
                                        int hostIndex)
{
	if (hostIndex < 0 || hostIndex >= PlayerMax) {
		return;
	}
	const int playerIndex = NetworkGetPlayerIndexForHost(hostIndex);
	serverSetup.Ready[hostIndex] = clientSetup.Ready[hostIndex];
	if (playerIndex != -1) {
		serverSetup.ServerGameSettings.Presets[playerIndex].Race =
			clientSetup.ServerGameSettings.Presets[playerIndex].Race;
	}
}

bool CClient::Update_synced(unsigned long tick)
{
	Assert(networkState.State == ccs_synced);

	if (NetworkIsLocalSetupInSync(*serverSetup, *localSetup, NetLocalHostsSlot) == false) {
		networkState.State = ccs_changed;
		networkState.MsgCnt = 0;
		return Update(tick);
	}
	Send_Waiting(tick, 850);
	return true;
}

bool CClient::Update_changed(unsigned long tick)
{
	Assert(networkState.State == ccs_changed);

	if (networkState.MsgCnt < 20) { // 20 retries
		Send_State(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_changed: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_async(unsigned long tick)
{
	Assert(networkState.State == ccs_async);

	if (networkState.MsgCnt < 20) { // 20 retries
		Send_Resync(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_async: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_mapinfo(unsigned long tick)
{
	Assert(networkState.State == ccs_mapinfo);

	if (networkState.MsgCnt < 20) { // 20 retries
		// ICMMapAck..
		Send_Map(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_mapinfo: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_badmap(unsigned long tick)
{
	Assert(networkState.State == ccs_badmap);

	if (networkState.MsgCnt < 20) { // 20 retries
		Send_MapUidMismatch(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_badmap: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_needmap(unsigned long tick)
{
	Assert(networkState.State == ccs_needmap);

	if (networkState.MsgCnt < 50) {
		Send_MapNeeded(tick, networkState.StateArg);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_needmap: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_goahead(unsigned long tick)
{
	Assert(networkState.State == ccs_goahead);

	if (networkState.MsgCnt < 50) { // 50 retries
		if (meshProbe) {
			Send_Probes(tick);
		}
		Send_Config(tick);
		return true;
	} else {
		networkState.State = ccs_unreachable;
		DebugPrint("ccs_goahead: Above message limit %d\n", networkState.MsgCnt);
		return false;
	}
}

bool CClient::Update_started(unsigned long tick)
{
	Assert(networkState.State == ccs_started);

	if (networkState.MsgCnt < 20) { // 20 retries
		Send_Go(tick);
		return true;
	} else {
		return false; // End the menu..
	}
}

void CClient::Send_Go(unsigned long tick)
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMGo);

	SendRateLimited(message, tick, 250);
}

void CClient::Send_Config(unsigned long tick)
{
	if (meshProbe) {
		// the server waits for the peers we reach
		const CInitMessage_ConfigAck message(NetMeshProbeMask);

		SendRateLimited(message, tick, 250);
		return;
	}
	const CInitMessage_Header message(MessageInit_FromClient, ICMConfig);

	SendRateLimited(message, tick, 250);
}

/**
**  Probe every other client which has not answered yet. The server is not probed:
**  the Config came from it.
*/
void CClient::Send_Probes(unsigned long tick)
{
	if (lastProbeTick && tick - lastProbeTick < MESH_PROBE_INTERVAL) {
		return;
	}
	lastProbeTick = std::max(tick, 1ul);
	unsigned char buf[mesh::kWireProbeSize];
	const size_t size = mesh::SerializeProbe(false, NetLocalHostsSlot, buf, sizeof(buf));
	if (size == 0) {
		return; // our slot does not fit on the wire, the server will not start a mesh
	}
	for (int i = 1; i < mesh::kMaxPlayers; ++i) {
		if (i == NetLocalHostsSlot || !Hosts[i].IsValid() || (NetMeshProbeMask & (1 << i))) {
			continue;
		}
		const CHost host(Hosts[i].Host, Hosts[i].Port);
		socket->Send(host, buf, size);
	}
}

void CClient::Send_MapUidMismatch(unsigned long tick)
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMMapUidMismatch); // MAP Uid doesn't match

	SendRateLimited(message, tick, 650);
}

void CClient::Send_MapNeeded(unsigned long tick, unsigned int fragment, bool limit)
{
	const CInitMessage_MapFileFragment message((uint32_t)fragment); // Request map files from server

	SendRateLimited(message, tick, limit ? 1000 : 0);
}

void CClient::Send_Map(unsigned long tick)
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMMap);

	SendRateLimited(message, tick, 650);
}

void CClient::Send_Resync(unsigned long tick)
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMResync);

	SendRateLimited(message, tick, 450);
}

void CClient::Send_State(unsigned long tick)
{
	const CInitMessage_State message(MessageInit_FromClient, *localSetup);

	SendRateLimited(message, tick, 450);
}

void CClient::Send_Waiting(unsigned long tick, unsigned long msec)
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMWaiting);

	SendRateLimited(message, tick, msec);
}

void CClient::Send_Hello(unsigned long tick)
{
	const CInitMessage_Hello message(name.c_str());

	SendRateLimited(message, tick, 500);
}

void CClient::Send_GoodBye(unsigned long tick)
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMGoodBye);

	SendRateLimited(message, tick, 100);
}

/*
** @return false when client has finished.
*/
bool CClient::Update(unsigned long tick)
{
	switch (networkState.State) {
		case ccs_disconnected: return Update_disconnected();
		case ccs_detaching: return Update_detaching(tick);
		case ccs_connecting: return Update_connecting(tick);
		case ccs_connected: return Update_connected(tick);
		case ccs_synced: return Update_synced(tick);
		case ccs_changed: return Update_changed(tick);
		case ccs_async: return Update_async(tick);
		case ccs_mapinfo: return Update_mapinfo(tick);
		case ccs_badmap: return Update_badmap(tick);
		case ccs_needmap: return Update_needmap(tick);
		case ccs_goahead: return Update_goahead(tick);
		case ccs_started: return Update_started(tick);
		default: break;
	}
	return true;
}

void CClient::SetConfig(const CInitMessage_Config &msg)
{
	NetPlayers = 0;
	// the local host slot may have changed due to Hosts array compaction on the server
	NetLocalHostsSlot = msg.clientIndex;
	for (int i = 0; i < PlayerMax; ++i) {
		Hosts[i] = msg.hosts[i];
		if (Hosts[i].IsValid()) {
			NetPlayers++;
		}
		const std::string hostStr = CHost(msg.hosts[i].Host, msg.hosts[i].Port).toString();
		DebugPrint("%s %d = %s [%.*s]\n",
		           (i == 0 ? "Server" : (i == NetLocalHostsSlot ? "SELF" : "Client")),
		           msg.hosts[i].PlyNr,
		           hostStr.c_str(),
		           static_cast<int>(sizeof(msg.hosts[i].PlyName)),
		           msg.hosts[i].PlyName);
	}
	NetLocalPlayerNumber = msg.hosts[NetLocalHostsSlot].PlyNr;
	// server is first, set our view of ip:port
	// (the server does not know its own address: its entry arrives as 0:0)
	Hosts[0].Host = serverHost.getIp();
	Hosts[0].Port = serverHost.getPort();
	// for ourselves we use the loopback ip:port
	// (never sent to: the own entry is skipped by slot; the other clients are held as the server sees them)
	Hosts[NetLocalHostsSlot].Host = INADDR_LOOPBACK; // FIXME: use CNetworkParameter::Instance.localHost, but also in InitNetwork1
	Hosts[NetLocalHostsSlot].Port = CNetworkParameter::Instance.localPort;
	const std::string serverHostStr = serverHost.toString();
	// the mesh is on only when the final state of the server says so
	NetMesh = false;
	meshProbe = msg.mesh != 0;
	NetMeshProbeMask = 0;
	lastProbeTick = 0;
}

bool CClient::Parse(const unsigned char *buf, const CHost &host)
{
	CInitMessage_Header header;
	header.Deserialize(buf);

	if (header.GetType() != MessageInit_FromServer) {
		return true;
	}
	// Assert(host == this->serverHost);
	const unsigned char msgsubtype = header.GetSubType();

	DebugPrint("Received %s in state %s\n",
	           icmsgsubtypenames[msgsubtype],
	           ncconstatenames[networkState.State]);

	switch (msgsubtype) {
		case ICMServerQuit: { // Server user canceled, should work in all states
			networkState.State = ccs_serverquits;
			// No ack here - Server will spew out a few Quit msgs, which has to be enough
			return false;
		}
		case ICMAYT: { // Server is checking for our presence
			Parse_AreYouThere();
			break;
		}
		case ICMGoodBye: { // Server has let us go
			if (networkState.State == ccs_detaching) {
				networkState.State = ccs_disconnected;
				networkState.MsgCnt = 0;
			}
			break;
		}
		case ICMEngineMismatch: { // Stratagus engine version doesn't match
			Parse_EngineMismatch(buf);
			return false;
		}
		case ICMLuaFilesMismatch: { // Network protocol version doesn't match
			Parse_LuaMismatch(buf);
			return false;
		}
		case ICMGameFull: { // Game is full - server rejected connnection
			Parse_GameFull();
			return false;
		}
		case ICMWelcome: { // Server has accepted us
			Parse_Welcome(buf);
			break;
		}
		case ICMMap: { // Server has sent us new map info
			Parse_Map(buf);
			break;
		}
		case ICMMapNeeded: { // Server has sent us new map data
			Parse_MapFragment(buf);
			break;
		}
		case ICMState: {
			Parse_State(buf);
			break;
		}
		case ICMConfig: { // Server gives the go ahead.. - start game
			Parse_Config(buf);
			break;
		}
		case ICMResync: { // Server has resynced with us and sends resync data
			Parse_Resync(buf);
			break;
		}
		case ICMGo: { // Server's final go ..
			// ccs_started
			DebugPrint("ClientParseStarted ICMGo !!!!!\n");
			return false;
		}
		default: break;
	}
	return true;
}

/**
** Check if the map name looks safe.
**
** A map name looks safe when there are no special characters
** and no .. or // sequences. This way only real valid
** maps from the map directory will be loaded.
**
** @return  true if the map name looks safe.
*/
static bool IsSafeMapName(const char *mapname)
{
	char buf[256];

	if (strncpy_s(buf, sizeof(buf), mapname, sizeof(buf)) != 0) {
		return false;
	}
	if (strstr(buf, "..")) {
		return false;
	}
	if (strstr(buf, "//")) {
		return false;
	}
	if (buf[0] == '\0') {
		return false;
	}

	for (const char *ch = buf; *ch != '\0'; ++ch) {
		if (!isalnum(*ch) && *ch != '/' && *ch != '.' && *ch != '-'
			&& *ch != '(' && *ch != ')' && *ch != '_' && *ch != ' ') {
			return false;
		}
	}
	return true;
}

void CClient::Parse_Map(const unsigned char *buf)
{
	if (networkState.State != ccs_connected) {
		return;
	}
	CInitMessage_Map msg;

	msg.Deserialize(buf);
	if (!IsSafeMapName(msg.MapPath)) {
		ErrorPrint("Unsecure map name!\n");
		networkState.State = ccs_badmap;
		return;
	}
	NetworkMapName = std::string(msg.MapPath, sizeof(msg.MapPath));
	// LAN-JOIN-MAP: a map of the game folder (Data/Maps/<name>.pud) is not a file under the
	// library path - the host loads it by its plain name through the native PUD loader, and so
	// must the joiner, or every game on such a map ends in ccs_needmap.
	const std::string plainName = NetworkMapName.c_str();   // without the message's padding
	const fs::path mappath = !ResolveMapPud(plainName).empty()
	                         ? fs::path(plainName)
	                         : fs::path(StratagusLibPath) / NetworkMapName;
	if (!LoadStratagusMapInfo(mappath) && !networkState.StateArg) {
		networkState.State = ccs_needmap;
		networkState.MsgCnt = 0;
		return;
	} else if (msg.MapUID != Map.Info.MapUID) {
		networkState.State = ccs_badmap;
		ErrorPrint("Stratagus maps do not match (0x%08x) <-> (0x%08x)\n",
		           Map.Info.MapUID,
		           static_cast<unsigned int>(msg.MapUID));
		return;
	}
	networkState.State = ccs_mapinfo;
	networkState.MsgCnt = 0;
}

void CClient::Parse_MapFragment(const unsigned char *buf)
{
	if (networkState.State != ccs_needmap) {
		return;
	}
	CInitMessage_MapFileFragment msg;

	msg.Deserialize(buf);

	if (msg.FragmentIndex < networkState.StateArg) {
		// this is a udp package from a fragment we already have, ignore
		networkState.State = ccs_needmap;
		return;
	}

	if (msg.FragmentIndex > networkState.StateArg) {
		// we got a fragment that is newer, too new, that is bad
		networkState.State = ccs_badmap;
		ErrorPrint(
			"Missed map fragment %d, already got %d\n", networkState.StateArg, msg.FragmentIndex);
		return;
	}

	if (msg.PathSize == 0) {
		// this is the last fragment, nothing left, we got the map.
		// go back to the state just after connecting
		networkState.State = ccs_connected;
		networkState.MsgCnt = 0;
		networkState.StateArg = 1; // set to 1 as a flag that we don't try receiving the map again
		return;
	}

	// this is the fragment we requested, open the file and write it

	// FIXME: what if the file exists before even the first fragment creates it?

	fs::path mappath(StratagusLibPath);
	// msg.PathSize is 8bits, so smaller than msg.Data by construction
	NetworkMapFragmentName = std::string(msg.Data, msg.PathSize);
	if (NetworkMapFragmentName.find("..") != std::string::npos) {
		// bad path
		networkState.State = ccs_badmap;
		ErrorPrint("Bad network filename '%s'\n", NetworkMapFragmentName.c_str());
		return;
	}
	mappath /= NetworkMapFragmentName;

	std::ofstream mapfile(mappath.c_str(), std::ios::out | std::ios::app | std::ios::binary);
	if (!mapfile.is_open()) {
		networkState.State = ccs_badmap;
		ErrorPrint("Could not open '%s' for appending map data\n", mappath.u8string().c_str());
		return;
	} else {
		Assert(msg.DataSize + msg.PathSize <= sizeof(msg.Data));
		mapfile.write(msg.Data + msg.PathSize, msg.DataSize);
		mapfile.close();
	}

	networkState.State = ccs_needmap;
	networkState.StateArg++;
	networkState.MsgCnt = 0;

	// immediately ask for the next one
	Send_MapNeeded(networkState.LastFrame, networkState.StateArg, false);
}

void CClient::Parse_Welcome(const unsigned char *buf)
{
	if (networkState.State != ccs_connecting) {
		return;
	}
	CInitMessage_Welcome msg;

	msg.Deserialize(buf);
	networkState.State = ccs_connected;
	networkState.MsgCnt = 0;
	networkState.StateArg = 0;
	NetLocalHostsSlot = msg.NetHostSlot;
	CNetworkParameter::Instance.NetworkLag = msg.Lag;
	CNetworkParameter::Instance.gameCyclesPerUpdate = msg.gameCyclesPerUpdate;

	OnlineContextHandler->joinGame(msg.hosts[0].PlyName, "");

	for (int i = 0; i < PlayerMax; ++i) {
		Hosts[i] = msg.hosts[i];
		if (i == NetLocalHostsSlot) {
			Assert(name == Hosts[i].PlyName);
		}
	}
	// server is first, set ip:port from our perspective
	Hosts[0].Host = serverHost.getIp();
	Hosts[0].Port = serverHost.getPort();
	// for ourselves we use the loopback ip:port
	Hosts[NetLocalHostsSlot].Host = INADDR_LOOPBACK; // FIXME: use CNetworkParameter::Instance.localHost, but also in InitNetwork1
	Hosts[NetLocalHostsSlot].Port = CNetworkParameter::Instance.localPort;
}

void CClient::Parse_State(const unsigned char *buf)
{
	CInitMessage_State msg;

	msg.Deserialize(buf);
	if (networkState.State == ccs_mapinfo) {
		// Server has sent us first state info
		*serverSetup = msg.State;
		networkState.State = ccs_synced;
		networkState.MsgCnt = 0;
	} else if (networkState.State == ccs_synced
			   || networkState.State == ccs_changed) {
		*serverSetup = msg.State;
		networkState.State = ccs_async;
		networkState.MsgCnt = 0;
	} else if (networkState.State == ccs_goahead) {
		// Server has sent final state info
		*serverSetup = msg.State;
		NetMesh = meshProbe && msg.mesh != 0;
		networkState.State = ccs_started;
		networkState.MsgCnt = 0;
	}
}

void CClient::Parse_Config(const unsigned char *buf)
{
	if (networkState.State != ccs_synced) {
		return;
	}
	CInitMessage_Config msg;

	msg.Deserialize(buf);
	SetConfig(msg);
	networkState.State = ccs_goahead;
	networkState.MsgCnt = 0;
}

void CClient::Parse_Resync(const unsigned char *buf)
{
	if (networkState.State != ccs_async) {
		return;
	}
	CInitMessage_Resync msg;

	msg.Deserialize(buf);
	for (int i = 0; i < PlayerMax - 1; ++i) {
		Hosts[i] = msg.hosts[i];
		if (i == NetLocalHostsSlot) {
			Assert(name == Hosts[i].PlyName);
		}
	}
	networkState.State = ccs_synced;
	networkState.MsgCnt = 0;
}

void CClient::Parse_GameFull()
{
	if (networkState.State != ccs_connecting) {
		return;
	}
	const std::string serverHostStr = serverHost.toString();
	ErrorPrint("Server at '%s' is full!\n", serverHostStr.c_str());
	networkState.State = ccs_nofreeslots;
}

void CClient::Parse_LuaMismatch(const unsigned char *buf)
{
	if (networkState.State != ccs_connecting) {
		return;
	}
	CInitMessage_LuaFilesMismatch msg;

	msg.Deserialize(buf);
	const std::string serverHostStr = serverHost.toString();
	ErrorPrint("Incompatible Lua files version %d <-> %d \nfrom '%s'\n",
	           FileChecksums,
	           msg.Version,
	           serverHostStr.c_str());
	networkState.State = ccs_incompatibleluafiles;
}

void CClient::Parse_EngineMismatch(const unsigned char *buf)
{
	if (networkState.State != ccs_connecting) {
		return;
	}
	CInitMessage_EngineMismatch msg;

	msg.Deserialize(buf);
	const std::string serverHostStr = serverHost.toString();
	ErrorPrint("Incompatible Stratagus version %d <-> %d\nfrom '%s'\n",
	           NetworkProtocolVersion,
	           msg.Stratagus,
	           serverHostStr.c_str());
	networkState.State = ccs_incompatibleengine;
}

/**
** Parse a network menu AreYouThere keepalive packet and reply IAmHere.
**
** @param msg message received
*/
void CClient::Parse_AreYouThere()
{
	const CInitMessage_Header message(MessageInit_FromClient, ICMIAH); // IAmHere

	NetworkSendICMessage(*socket, serverHost, message);
}

//
// CServer
//

void CServer::KickClient(int c)
{
	DebugPrint("kicking client %d, player number %d\n", c, Hosts[c].PlyNr);
	const int playerIndex = NetworkGetPlayerIndexForHost(c);
	Hosts[c].Clear();
	serverSetup->Ready[c] = 0;
	if (playerIndex != -1) {
		serverSetup->ServerGameSettings.Presets[playerIndex].Race = 0;
	}
	networkStates[c].Clear();
	// Resync other clients
	for (int n = 1; n < PlayerMax; ++n) {
		if (Hosts[n].IsValid()) {
			networkStates[n].State = ccs_async;
		}
	}
}

void CServer::Init(const std::string &name, CUDPSocket *socket, CServerSetup *serverSetup)
{
	for (int i = 0; i < PlayerMax; ++i) {
		networkStates[i].Clear();
		if (i) { // don't clear ourselves
			Hosts[i].Clear();
		}
	}
	this->serverSetup = serverSetup;
	this->name = name;
	this->socket = socket;
}

void CServer::Send_AreYouThere(const CNetworkHost &host)
{
	const CInitMessage_Header message(MessageInit_FromServer, ICMAYT); // AreYouThere

	NetworkSendICMessage(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Send_GameFull(const CHost &host)
{
	const CInitMessage_Header message(MessageInit_FromServer, ICMGameFull);

	NetworkSendICMessage_Log(*socket, host, message);
}

void CServer::Send_Welcome(const CNetworkHost &host, int index)
{
	CInitMessage_Welcome message;

	for (int i = 0; i < PlayerMax; ++i) { // Info about clients
		message.hosts[i] = Hosts[i];
	}
	message.NetHostSlot = index;
	NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Send_Resync(const CNetworkHost &host, int hostIndex)
{
	CInitMessage_Resync message;

	for (int i = 0; i < PlayerMax; ++i) { // Info about clients
		message.hosts[i] = Hosts[i];
	}
	NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Send_Map(const CNetworkHost &host)
{
	const CInitMessage_Map message(NetworkMapName.c_str(), Map.Info.MapUID);

	NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Send_MapFragment(const CNetworkHost &host, uint32_t fragmentIdx)
{
	fs::path prefix = fs::path(NetworkMapName);
	while (prefix.stem() != prefix) { // may have 	.gz, .bz2 ...
		prefix = prefix.stem();
	}
	fs::path mapDirectory(StratagusLibPath);
	mapDirectory /= NetworkMapName;
	mapDirectory = mapDirectory.parent_path();

	std::set<fs::path> sortedFilenames;
	for (const auto &entry : fs::directory_iterator(mapDirectory)) {
		fs::path entryPath(entry.path());
		while (entryPath.stem() != entryPath) {
			entryPath = entryPath.stem();
		}
		if (entryPath == prefix) {
			sortedFilenames.insert(entry.path());
		}
	}

	fs::path currentPath;
	std::string networkName;
	uint32_t fragmentDataSize = 0;
	uint32_t fileSize = 0;
	uint32_t fragmentIdxStartForFile = 0;
	fs::path libPath(StratagusLibPath);

	for (fs::path p : sortedFilenames) {
		currentPath = p;

		// work around fs::relative not being available in some experimental fs impls
		fs::path networkPathEnd(p.filename());
		fs::path networkPathStart(p.parent_path());
		while (networkPathStart != libPath) {
			networkPathEnd = *--networkPathStart.end() / networkPathEnd;
			networkPathStart = networkPathStart.parent_path();
		}
		networkName = networkPathEnd.generic_u8string();

		fragmentDataSize = sizeof(CInitMessage_MapFileFragment::Data) - networkName.size();
		fileSize = fs::file_size(p);
		uint32_t fragmentCountForFile = (fileSize + fragmentDataSize - 1) / fragmentDataSize;
		if ((fragmentIdxStartForFile <= fragmentIdx) && (fragmentIdx < fragmentIdxStartForFile + fragmentCountForFile)) {
			break;
		} else {
			// next file starts at the next fragment
			fragmentIdxStartForFile += fragmentCountForFile;
			fragmentDataSize = 0;
		}
	}

	if (fragmentDataSize > 0) {
		std::ifstream file(currentPath.c_str(), std::ios::in | std::ios::binary);
		if (file.is_open()) {
			uint32_t offset = (fragmentIdx - fragmentIdxStartForFile) * fragmentDataSize;
			file.seekg(offset);
			fragmentDataSize = std::min<uint32_t>(fragmentDataSize, fileSize - offset);
			std::vector<char> data(fragmentDataSize);
			file.read(data.data(), data.size());
			file.close();

			DebugPrint("Sending map fragment %d for %s (size %d, offset %d)\n",
			           fragmentIdx,
			           networkName.c_str(),
			           data.size(),
			           offset);
			const CInitMessage_MapFileFragment message(networkName, data, fragmentIdx);
			NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
			return;
		} else {
			// FIXME: ouch! we cannot read this map file. very strange, and very bad
			// fall through for now and end the transmission
		}
	}

	// no file content found for this fragment index, we're done
	DebugPrint("Sending end fragment %d for %s\n", fragmentIdx, NetworkMapName.c_str());
	const CInitMessage_MapFileFragment message("", {}, fragmentIdx);
	NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Send_State(const CNetworkHost &host)
{
	const CInitMessage_State message(MessageInit_FromServer, *serverSetup);

	NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Send_GoodBye(const CNetworkHost &host)
{
	const CInitMessage_Header message(MessageInit_FromServer, ICMGoodBye);

	NetworkSendICMessage_Log(*socket, CHost(host.Host, host.Port), message);
}

void CServer::Update(unsigned long frameCounter)
{
	for (int i = 1; i < PlayerMax; ++i) {
		if (Hosts[i].IsValid() && Hosts[i].Host && Hosts[i].Port) {
			const unsigned long fcd = frameCounter - networkStates[i].LastFrame;
			if (fcd >= CLIENT_LIVE_BEAT) {
				if (fcd > CLIENT_IS_DEAD) {
					KickClient(i);
				} else if (fcd % 5 == 0) {
					// Probe for the client
					Send_AreYouThere(Hosts[i]);
				}
			}
		}
	}
}

void CServer::MarkClientsAsResync()
{
	for (int i = 1; i < PlayerMax - 1; ++i) {
		if (Hosts[i].IsValid() && networkStates[i].State == ccs_synced) {
			networkStates[i].State = ccs_async;
		}
	}
}

/**
**  Whether every connected client has taken over the present setup (is in sync).
**  A game must not be started before: a client that still waits for its resync answer
**  gets none once the start has begun.
*/
bool CServer::AllClientsSynced() const
{
	for (int i = 1; i < PlayerMax - 1; ++i) {
		if (Hosts[i].IsValid() && networkStates[i].State != ccs_synced) {
			return false;
		}
	}
	return true;
}

/**
**  Parse the initial 'Hello' message of new client that wants to join the game
**
**  @param h slot number of host msg originates from
**  @param msg message received
**  @param host  host which send the message
**
**  @return host index
*/
int CServer::Parse_Hello(int h, const CInitMessage_Hello &msg, const CHost &host)
{
	if (h == -1) { // it is a new client
		std::vector<int> availableSlots;
		for (int i = 0; i < PlayerMax; ++i) {
			if (serverSetup->CompOpt[i] == SlotOption::Available) {
				availableSlots.push_back(i);
			}
		}
		for (int i = 0; i < PlayerMax; ++i) {
			if (availableSlots.empty()) {
				break;
			} else if (!Hosts[i].IsValid()) {
				h = i;
				break;
			} else {
				ranges::erase(availableSlots, Hosts[i].PlyNr);
			}
		}
		if (h != -1) {
			Hosts[h].Host = host.getIp();
			Hosts[h].Port = host.getPort();
			Hosts[h].PlyNr = availableSlots.front();
			Hosts[h].SetName(msg.PlyName);
			const std::string hostStr = host.toString();
			DebugPrint("New client %s [%s]\n", hostStr.c_str(), Hosts[h].PlyName);
			networkStates[h].State = ccs_connecting;
			networkStates[h].MsgCnt = 0;
		} else {
			// Game is full - reject connnection
			Send_GameFull(host);
			return -1;
		}
	}
	// this code path happens until client sends waiting (= has received this message)
	Send_Welcome(Hosts[h], h);

	networkStates[h].MsgCnt++;
	if (networkStates[h].MsgCnt > 48) {
		// Detects UDP input firewalled or behind NAT firewall clients
		// If packets are missed, clients are kicked by AYT check later..
		KickClient(h);
		return -1;
	}
	return h;
}

/**
**  Parse client resync request after client user has changed menu selection
**
**  @param h slot number of host msg originates from
*/
void CServer::Parse_Resync(const int h)
{
	switch (networkStates[h].State) {
		case ccs_mapinfo: // a delayed ack..
			[[fallthrough]];
		case ccs_async:
			// client has recvd welcome and is waiting for info
			networkStates[h].State = ccs_synced;
			networkStates[h].MsgCnt = 0;
			[[fallthrough]];
		case ccs_synced: {
			// this code path happens until client falls back to ICMWaiting
			// (indicating Resync has completed)
			Send_Resync(Hosts[h], h);

			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 50) {
				// FIXME: Client sends resync, but doesn't receive our resync ack....
			}
			break;
		}
		default:
			DebugPrint(
				"Server: ICMResync: Unhandled state %d Host %d\n", networkStates[h].State, h);
			break;
	}
}

/**
**  Parse client heart beat waiting message
**
**  @param h slot number of host msg originates from
*/
void CServer::Parse_Waiting(const int h)
{
	switch (networkStates[h].State) {
		// client has recvd welcome and is waiting for info
		case ccs_connecting:
			networkStates[h].State = ccs_connected;
			networkStates[h].MsgCnt = 0;
			[[fallthrough]];
		case ccs_needmap: // client has finished receiving the map and wants the info again
			networkStates[h].State = ccs_connected;
			networkStates[h].MsgCnt = 0;
			[[fallthrough]];
		case ccs_connected: {
			// this code path happens until client acknowledges the map
			Send_Map(Hosts[h]);

			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 50) {
				// FIXME: Client sends waiting, but doesn't receive our map....
			}
			break;
		}
		case ccs_mapinfo:
			networkStates[h].State = ccs_synced;
			networkStates[h].MsgCnt = 0;
			for (int i = 1; i < PlayerMax; ++i) {
				if (i != h && Hosts[i].IsValid()) {
					// Notify other clients
					networkStates[i].State = ccs_async;
				}
			}
			[[fallthrough]];
		case ccs_synced:
			// the wanted state - do nothing.. until start...
			networkStates[h].MsgCnt = 0;
			break;

		case ccs_async: {
			// Server User has changed menu selection. This state is set by MENU code
			// OR we have received a new client/other client has changed data

			// this code path happens until client acknoledges the state change
			// by sending ICMResync
			Send_State(Hosts[h]);

			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 50) {
				// FIXME: Client sends waiting, but doesn't receive our state info....
			}
			break;
		}
		default:
			DebugPrint(
				"Server: ICMWaiting: Unhandled state %d Host %d\n", networkStates[h].State, h);
			break;
	}
}

/**
**  Parse client map info acknoledge message
**
**  @param h slot number of host msg originates from
*/
void CServer::Parse_Map(const int h)
{
	switch (networkStates[h].State) {
		// client has recvd map info waiting for state info
		case ccs_connected:
		case ccs_async:
			networkStates[h].State = ccs_mapinfo;
			networkStates[h].MsgCnt = 0;
			[[fallthrough]];
		case ccs_mapinfo: {
			// this code path happens until client acknowledges the state info
			// by falling back to ICMWaiting with prev. State synced
			Send_State(Hosts[h]);

			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 50) {
				// FIXME: Client sends mapinfo, but doesn't receive our state info....
			}
			break;
		}
		default:
			DebugPrint("Server: ICMMap: Unhandled state %d Host %d\n", networkStates[h].State, h);
			break;
	}
}

void CServer::Parse_MapFragment(const int h, uint32_t fragmentIdx)
{
	switch (networkStates[h].State) {
		// client has recvd map info but needs the map
		case ccs_connected:
			networkStates[h].State = ccs_needmap;
			networkStates[h].MsgCnt = 0;
			Assert(fragmentIdx == 0); // client keep asking for this fragment initially
			[[fallthrough]];
		case ccs_needmap: {
			Send_MapFragment(Hosts[h], fragmentIdx);
			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 50) {
				// FIXME: Client asks for map, but doesn't receive our fragment ....
			}
			break;
		}
		default:
			DebugPrint("Server: ICMMap: Unhandled state %d Host %d\n", networkStates[h].State, h);
			break;
	}
}

/**
**  Parse locate state change notifiction or initial state info request of client
**
**  @param h slot number of host msg originates from
**  @param msg message received
*/
void CServer::Parse_State(const int h, const CInitMessage_State &msg)
{
	switch (networkStates[h].State) {
		case ccs_mapinfo:
		// User State Change right after connect - should not happen, but..
			[[fallthrough]];
		case ccs_synced: {
			// Default case: Client is in sync with us, but notes a local change
			// networkStates[h].State = ccs_async;
			networkStates[h].MsgCnt = 0;
			// Use information supplied by the client:
			NetworkApplyClientSetupStateChange(*serverSetup, msg.State, h);
			// Add additional info usage here!

			// Resync other clients (and us..)
			for (int i = 1; i < PlayerMax; ++i) {
				if (Hosts[i].IsValid()) {
					networkStates[i].State = ccs_async;
				}
			}
			[[fallthrough]];
		}
		case ccs_async: {
			// this code path happens until client acknowledges the state change reply
			// by sending ICMResync
			Send_State(Hosts[h]);

			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 50) {
				// FIXME: Client sends State, but doesn't receive our state info....
			}
			break;
		}
		default:
			DebugPrint("Server: ICMState: Unhandled state %d Host %d\n", networkStates[h].State, h);
			break;
	}
}

/**
**  Parse the disconnect request of a client by sending out good bye
**
**  @param h slot number of host msg originates from
*/
void CServer::Parse_GoodBye(const int h)
{
	switch (networkStates[h].State) {
		default:
			// We can enter here from _ANY_ state!
			networkStates[h].MsgCnt = 0;
			networkStates[h].State = ccs_detaching;
			[[fallthrough]];
		case ccs_detaching: {
			// this code path happens until client acknoledges the GoodBye
			// by sending ICMSeeYou;
			Send_GoodBye(Hosts[h]);

			networkStates[h].MsgCnt++;
			if (networkStates[h].MsgCnt > 10) {
				// FIXME: Client sends GoodBye, but doesn't receive our GoodBye....
			}
			break;
		}
	}
}

/**
** Parse the final see you msg of a disconnecting client
**
** @param h slot number of host msg originates from
*/
void CServer::Parse_SeeYou(const int h)
{
	switch (networkStates[h].State) {
		case ccs_detaching:
			KickClient(h);
			break;

		default:
			DebugPrint(
				"Server: ICMSeeYou: Unhandled state %d Host %d\n", networkStates[h].State, h);
			break;
	}
}

/**
**  Check if the Stratagus version and Network Protocol match
**
**  @param msg message received
**  @param host  host which send the message
**
**  @return 0 if the versions match, -1 otherwise
*/
static int CheckVersions(const CInitMessage_Hello &msg, CUDPSocket &socket, const CHost &host)
{
	if (msg.Stratagus != NetworkProtocolVersion) {
		const std::string hostStr = host.toString();
		ErrorPrint("Incompatible Stratagus version %d <-> %d from '%s'\n",
		           NetworkProtocolVersion,
		           msg.Stratagus,
		           hostStr.c_str());

		const CInitMessage_EngineMismatch message;
		NetworkSendICMessage_Log(socket, host, message);
		return -1;
	}

	if (msg.Version != FileChecksums) {
		const std::string hostStr = host.toString();
		ErrorPrint("Incompatible lua files %d <-> %d\nfrom '%s'\n",
		           FileChecksums,
		           msg.Version,
		           hostStr.c_str());

		const CInitMessage_LuaFilesMismatch message;
		NetworkSendICMessage_Log(socket, host, message);
		return -1;
	}
	return 0;
}

void CServer::Parse(unsigned long frameCounter, const unsigned char *buf, const CHost &host)
{
	const unsigned char msgsubtype = buf[1];
	int index = FindHostIndexBy(host);

	if (index == -1) {
		if (msgsubtype == ICMHello) {
			CInitMessage_Hello msg;

			msg.Deserialize(buf);
			if (CheckVersions(msg, *socket, host)) {
				return;
			}
			// Special case: a new client has arrived
			index = Parse_Hello(-1, msg, host);
			networkStates[index].LastFrame = frameCounter;
		}
		return;
	}
	networkStates[index].LastFrame = frameCounter;
	switch (msgsubtype) {
		case ICMHello: { // a new client has arrived
			CInitMessage_Hello msg;

			msg.Deserialize(buf);
			Parse_Hello(index, msg, host);
			break;
		}
		case ICMResync: Parse_Resync(index); break;
		case ICMWaiting: Parse_Waiting(index); break;
		case ICMMap: Parse_Map(index); break;

		case ICMMapNeeded: {
			CInitMessage_MapFileFragment msg;
			msg.Deserialize(buf);
			Parse_MapFragment(index, msg.FragmentIndex);
			break;
		}

		case ICMState: {
			CInitMessage_State msg;

			msg.Deserialize(buf);
			Parse_State(index, msg);
			break;
		}
		case ICMMapUidMismatch: // Parse_MapUidMismatch(index, buf); break;
		case ICMGoodBye: Parse_GoodBye(index); break;
		case ICMSeeYou: Parse_SeeYou(index); break;
		case ICMIAH: break;

		default:
			DebugPrint("Server: Unhandled subtype %d from host %d\n", msgsubtype, index);
			break;
	}
}

//
// Functions
//

/**
**  Parse a setup event. (Command type <= MessageInitEvent)
**
**  @param buf Packet received
**  @param size size of the received packet.
**  @param host  host which send the message
**
**  @return true if packet is an InitConfig message, false otherwise
*/
bool NetworkParseSetupEvent(const unsigned char *buf, int size, const CHost &host)
{
	Assert(NetConnectRunning != 0);

	CInitMessage_Header header;
	header.Deserialize(buf);
	const unsigned char msgtype = header.GetType();
	if ((msgtype == MessageInit_FromClient && NetConnectRunning != 1)
		|| (msgtype == MessageInit_FromServer && NetConnectRunning != 2)) {
		if (NetConnectRunning == 2 && Client.GetNetworkState() == ccs_started) {
			// Client has acked ready to start and receives first real network packet.
			// This indicates that we missed the 'Go' in started state and the game
			// has been started by the server, so do the same for the client.
			NetConnectRunning = 0; // End the menu..
		}
		return false;
	}
#ifdef DEBUG
	const unsigned char msgsubtype = header.GetSubType();
	const std::string hostStr = host.toString();
	DebugPrint("Received %s (%d) from %s\n",
	           icmsgsubtypenames[int(msgsubtype)],
	           msgsubtype,
	           hostStr.c_str());
#endif
	if (NetConnectRunning == 2) { // client
		if (Client.Parse(buf, host) == false) {
			NetConnectRunning = 0;
		}
	} else if (NetConnectRunning == 1) { // server
		Server.Parse(FrameCounter, buf, host);
	}
	return true;
}

/**
** Client Menu Loop: Send out client request messages
*/
void NetworkProcessClientRequest()
{
	if (Client.Update(GetTicks()) == false) {
		NetConnectRunning = 0;
	}
}

int GetNetworkState()
{
	return Client.GetNetworkState();
}

int FindHostIndexBy(const CHost &host)
{
	for (int i = 0; i != PlayerMax; ++i) {
		if (Hosts[i].Host == host.getIp() && Hosts[i].Port == host.getPort()) {
			return i;
		}
	}
	return -1;
}

bool NetworkIsDedicatedServerHost(const CNetworkHost &host, const CServerSetup &setup)
{
	return host.IsValid() && host.PlyNr < PlayerMax
	       && setup.CompOpt[host.PlyNr] == SlotOption::Closed;
}

void NetworkCompactHosts()
{
	const auto endValid = std::remove_if(Hosts, Hosts + PlayerMax, [](const auto& host){ return !host.IsValid(); });
	for (auto it = endValid; it != Hosts + PlayerMax; ++it) {
		it->Clear();
	}
}

std::vector<int> NetworkRemoteHostIndices()
{
	std::vector<int> remoteHostIndices;
	for (int i = 1; i < PlayerMax; ++i) {
		if (Hosts[i].IsValid()) {
			remoteHostIndices.push_back(i);
		}
	}
	return remoteHostIndices;
}

/**
** Server Menu Loop: Send out server request messages
*/
void NetworkProcessServerRequest()
{
	if (GameRunning) {
		return;
		// Game already started...
	}

	MdnsService.AnswerServerQueries();
	Server.Update(FrameCounter);
}

/**
** Setup Network connect state machine for clients
*/
void NetworkInitClientConnect()
{
	NetConnectRunning = 2;
	NetConnectType = 2;
	NetMesh = false;

	for (int i = 0; i < PlayerMax; ++i) {
		Hosts[i].Clear();
	}
	ServerSetupState.Clear();
	LocalSetupState.Clear();
	Client.Init(Parameters::Instance.LocalPlayerName, &NetworkFildes, &ServerSetupState, &LocalSetupState, GetTicks());
}

/**
** Server user has finally hit the start game button.
** (This is run on the server)
**
** @return false if a client stopped answering: the start is given up.
*/
bool NetworkServerStartGame()
{
	// save it first..
	LocalSetupState = ServerSetupState;

	printf("INITIAL ServerSetupState:\n");
	NetPlayers = 0;
	if (Hosts[0].IsValid() && Hosts[0].PlyNr < PlayerMax
	    && Map.Info.PlayerType[Hosts[0].PlyNr] != PlayerTypes::PlayerPerson) {
		for (int i = 0; i < PlayerMax; ++i) {
			if (Map.Info.PlayerType[i] == PlayerTypes::PlayerPerson
			    && ServerSetupState.CompOpt[i] == SlotOption::Available) {
				const bool occupied = std::any_of(Hosts + 1, Hosts + PlayerMax, [i](const CNetworkHost &host) {
					return host.IsValid() && host.PlyNr == i;
				});
				if (!occupied) {
					Hosts[0].PlyNr = i;
					break;
				}
			}
		}
	}
	int compPlayers = ServerSetupState.ServerGameSettings.Opponents;
	const bool dedicatedServer = NetworkIsDedicatedServerHost(Hosts[0], ServerSetupState);
	const auto isHumanHostForPlayer = [dedicatedServer](const int hostIndex, const int playerIndex) {
		if (dedicatedServer && hostIndex == 0) {
			return false;
		}
		return Hosts[hostIndex].IsValid() && Hosts[hostIndex].PlyNr == playerIndex;
	};

	// most game settings are already fine, that is, they are on default. however, for human player slots, we need
	// to adapt the game settings from their defaults to either set the type to person if a human player is assigned,
	// computer if an AI player is assigned, or nobody if the slot will not be used in this game.
	for (int i = 0; i < PlayerMax; ++i) {
		printf("%02d: CO: %d   Race: %d   Host: ", i, (int)ServerSetupState.CompOpt[i], ServerSetupState.ServerGameSettings.Presets[i].Race);
		if (ServerSetupState.CompOpt[i] == SlotOption::Available) {
			bool hasHumanPlayer = false;
			for (int hostIndex = 0; hostIndex < PlayerMax; ++hostIndex) {
				if (isHumanHostForPlayer(hostIndex, i)) {
					const CNetworkHost &h = Hosts[hostIndex];
					NetPlayers++;
					hasHumanPlayer = true;
					ServerSetupState.ServerGameSettings.Presets[i].Type = PlayerTypes::PlayerPerson;
					const std::string hostStr = CHost(h.Host, h.Port).toString();
					printf(" %s %s", hostStr.c_str(), h.PlyName);
				}
			}
			if (!hasHumanPlayer && compPlayers) {
				compPlayers--;
				ServerSetupState.CompOpt[i] = LocalSetupState.CompOpt[i] = SlotOption::Computer;
				ServerSetupState.ServerGameSettings.Presets[i].Type = PlayerTypes::PlayerComputer;
				if (ServerSetupState.ServerGameSettings.Presets[i].AIScript == DEFAULT_PASSIVE_AI) {
					ServerSetupState.ServerGameSettings.Presets[i].AIScript = DEFAULT_ACTIVE_AI;
				}
			} else if (!hasHumanPlayer) {
				ServerSetupState.CompOpt[i] = LocalSetupState.CompOpt[i] = SlotOption::Closed;
				ServerSetupState.ServerGameSettings.Presets[i].Type = PlayerTypes::PlayerNobody;
			}
		}
		printf("\n");
	}

	// all players are assigned already, and the ServerSetupState should be valid already.
	// just assert that this is true.
	for (int i = 0; i < PlayerMax; ++i) {
		if (Map.Info.PlayerType[i] == PlayerTypes::PlayerPerson) {
			if (ServerSetupState.CompOpt[i] == SlotOption::Available) {
				bool hasHumanPlayer = false;
				for (int hostIndex = 0; hostIndex < PlayerMax; ++hostIndex) {
					if ((hasHumanPlayer = isHumanHostForPlayer(hostIndex, i))) {
						break;
					}
				}
				// if the slot is still marked as available, we should have a host for it
				Assert(hasHumanPlayer);
			}
		}
	}

	if (!NoRandomPlacementMultiplayer && NetPlayers) {
		// Randomize the player assignment.
		// It can be disabled by writing NoRandomPlacementMultiplayer() in lua files.
		DebugPrint("Randomizing player index assignments\n");
		std::vector<int> humanSlotIndices;
		std::vector<int> humanHostIndices;
		for (int i = 0; i < PlayerMax; i++) {
			if (Map.Info.PlayerType[i] == PlayerTypes::PlayerPerson
			    && ServerSetupState.CompOpt[i] != SlotOption::Closed) {
				humanSlotIndices.push_back(i);
			}
			if ((!dedicatedServer || i != 0) && Hosts[i].IsValid() && Hosts[i].PlyNr < PlayerMax
			    && ServerSetupState.CompOpt[Hosts[i].PlyNr] == SlotOption::Available
			    && ServerSetupState.ServerGameSettings.Presets[Hosts[i].PlyNr].Type
			           == PlayerTypes::PlayerPerson) {
				humanHostIndices.push_back(i);
			}
		}
		if (humanSlotIndices.empty() || humanHostIndices.empty()) {
			DebugPrint("Skipping random player assignment: no human slots or hosts\n");
		} else {
			std::random_device dev;
			std::mt19937 rng_engine(dev());
			std::uniform_int_distribution<size_t> hostDist(0, humanHostIndices.size() - 1);
			for (int i = 0; i < 100; i++) {
				std::shuffle(humanSlotIndices.begin(), humanSlotIndices.end(), rng_engine);
				auto &h = Hosts[humanHostIndices[hostDist(rng_engine)]];
				int newPlyNr = humanSlotIndices.front();
				int oldPlyNr = h.PlyNr;
				if (oldPlyNr == newPlyNr) {
					continue;
				}
				if (ServerSetupState.CompOpt[newPlyNr] == SlotOption::Available) {
					// a human host is currently assigned to this new player number, swap with it
					for (auto &otherH : Hosts) {
						if (otherH.PlyNr == newPlyNr) {
							std::swap(h.PlyNr, otherH.PlyNr);
							break;
						}
					}
				} else {
					// this newPlyNr was currently used by an AI or no one
					h.PlyNr = newPlyNr;
				}
				std::swap(ServerSetupState.CompOpt[oldPlyNr], ServerSetupState.CompOpt[newPlyNr]);
				std::swap(ServerSetupState.ServerGameSettings.Presets[oldPlyNr],
				          ServerSetupState.ServerGameSettings.Presets[newPlyNr]);
			}
		}
	}

	bool waitingForConfigAck[PlayerMax];
	bool waitingForInitAck[PlayerMax];
	std::fill_n(waitingForConfigAck, PlayerMax, true);
	std::fill_n(waitingForInitAck, PlayerMax, false);

	// Send all clients host:ports to all clients.
	// Slot 0 is the server.
	NetLocalPlayerNumber = Hosts[0].PlyNr;
	if (dedicatedServer) {
		bool foundLocalPlayer = false;
		for (int i = 0; i < PlayerMax; ++i) {
			if (ServerSetupState.CompOpt[i] == SlotOption::Computer
			    && ServerSetupState.ServerGameSettings.Presets[i].Type == PlayerTypes::PlayerComputer) {
				Hosts[0].PlyNr = i;
				NetLocalPlayerNumber = i;
				foundLocalPlayer = true;
				break;
			}
		}
		Assert(foundLocalPlayer);
	}

	NetworkCompactHosts();

	// Prepare the final config message:
	CInitMessage_Config message;
	for (int i = 0; i < PlayerMax; ++i) {
		message.hosts[i] = Hosts[i];
	}

	const std::vector<int> remoteHostIndices = NetworkRemoteHostIndices();
	// The mesh counts host slots, and its wire has room for mesh::kMaxPlayers of them.
	const bool meshAsked = NetMeshWanted
	                       && (remoteHostIndices.empty() || remoteHostIndices.back() < mesh::kMaxPlayers);
	message.mesh = meshAsked ? 1 : 0;
	NetMesh = false;
	NetMeshProbeFailed.clear();

	printf("FINAL ServerSetupState:\n");
	ServerSetupState.Save(+[](std::string f) { printf("%s", f.c_str()); });

	// Prepare the final state message:
	CInitMessage_State statemsg(MessageInit_FromServer, ServerSetupState);

	int hostsToAck = remoteHostIndices.size();
	DebugPrint("Ready, sending InitConfig to %d host(s)\n", hostsToAck);
	unsigned long lastAnswer[PlayerMax];
	std::fill_n(lastAnswer, PlayerMax, GetTicks());
	bool clientTimedOut = false;
	// The clients probe each other after the config; the final state waits for the result.
	uint16_t probeMask[PlayerMax];
	std::fill_n(probeMask, PlayerMax, 0);
	bool meshDecided = !meshAsked;
	unsigned long probeStart = 0; // tick of the last config ack, 0 = still waiting for one
	// Send all clients host:ports to all clients.
	while (hostsToAck) {
breakout:
		if (!meshDecided
		    && std::none_of(remoteHostIndices.begin(), remoteHostIndices.end(),
		                    [&](const int i) { return waitingForConfigAck[i]; })) {
			if (!probeStart) {
				probeStart = std::max(GetTicks(), 1ul);
			}
			// Every pair of clients must have heard each other, in both directions.
			NetMeshProbeFailed.clear();
			for (const int a : remoteHostIndices) {
				for (const int b : remoteHostIndices) {
					if (a != b && !(probeMask[a] & (1 << b))) {
						NetMeshProbeFailed.emplace_back(a, b);
					}
				}
			}
			if (NetMeshProbeFailed.empty() || GetTicks() - probeStart >= MESH_PROBE_TIMEOUT) {
				meshDecided = true;
				NetMesh = NetMeshProbeFailed.empty();
				statemsg.mesh = NetMesh ? 1 : 0;
				for (const auto &[a, b] : NetMeshProbeFailed) {
					ErrorPrint("NETWORK: mesh: client %d [%s] got no answer from client %d [%s]\n",
					           a, Hosts[a].PlyName, b, Hosts[b].PlyName);
				}
				if (!NetMesh) {
					ErrorPrint("NETWORK: mesh: not every pair of devices is reachable after %d ms,"
					           " the game runs over the server\n", MESH_PROBE_TIMEOUT);
				}
			}
		}
		// A client which stopped answering must not keep us here forever.
		for (const int i : remoteHostIndices) {
			if ((waitingForConfigAck[i] || waitingForInitAck[i])
			    && GetTicks() - lastAnswer[i] >= CLIENT_START_TIMEOUT) {
				ErrorPrint("NETWORK: No answer from client %d [%s] for %d ms, start aborted\n",
				           i, Hosts[i].PlyName, CLIENT_START_TIMEOUT);
				clientTimedOut = true;
			}
		}
		if (clientTimedOut) {
			break;
		}
		// Send to all clients
		for (const int i : remoteHostIndices) {
			if (Hosts[i].IsValid()) {
				const CHost host(message.hosts[i].Host, message.hosts[i].Port);
				if (waitingForConfigAck[i]) { // not acknowledged yet
					DebugPrint("Sending InitConfig to %s\n", host.toString().c_str());
					message.clientIndex = i;
					NetworkSendICMessage_Log(NetworkFildes, host, message);
				} else if (waitingForInitAck[i] && meshDecided) {
					DebugPrint("Sending InitState to %s\n", host.toString().c_str());
					NetworkSendICMessage_Log(NetworkFildes, host, statemsg);
				}
			}
		}

		// Wait for acknowledge
		std::array<unsigned char, CInitMessage_State::Size()> buf;
		while (hostsToAck && NetworkFildes.HasDataToRead(probeStart && !meshDecided ? MESH_PROBE_INTERVAL : 1000)) {
			CHost host;
			const int len = NetworkFildes.Recv(buf.data(), buf.size(), &host);
			if (len < 0) {
				const std::string hostStr = host.toString();
				DebugPrint("*Receive ack failed: (%d) from %s\n", len, hostStr.c_str());
				continue;
			}
			CInitMessage_Header header;
			header.Deserialize(buf.data());
			const unsigned char type = header.GetType();
			const unsigned char subtype = header.GetSubType();

			if (type == MessageInit_FromClient) {
				switch (subtype) {
					case ICMConfig: {
						const std::string hostStr = host.toString();
						DebugPrint("Got ack for InitConfig from %s\n", hostStr.c_str());
						const int index = FindHostIndexBy(host);
						if (index != -1) {
							lastAnswer[index] = GetTicks();
							if (meshAsked && len >= int(CInitMessage_ConfigAck::Size())) {
								CInitMessage_ConfigAck ack;

								ack.Deserialize(buf.data());
								probeMask[index] = ack.peers;
							}
							if (waitingForConfigAck[index]) {
								waitingForConfigAck[index] = false;
								waitingForInitAck[index] = true;
							}
							goto breakout;
						}
						break;
					}
					case ICMGo: {
						const std::string hostStr = host.toString();
						DebugPrint("Got ack for InitState from %s\n", hostStr.c_str());
						const int index = FindHostIndexBy(host);
						if (index != -1) {
							lastAnswer[index] = GetTicks();
							if (waitingForInitAck[index]) {
								waitingForInitAck[index] = false;
								hostsToAck--;
								DebugPrint("Removing host %d from waiting list\n", index);
							}
						}
						break;
					}
					default:
						DebugPrint("Server: Config ACK: Unhandled subtype %d\n", subtype);
						break;
				}
			} else {
				DebugPrint("Unexpected Message Type %d while waiting for Config ACK\n", type);
			}
		}
	}

	if (clientTimedOut) {
		// The clients hold the timed out client in their host table and cannot play
		// without it, so nobody starts: tell them and remove them here.
		const CInitMessage_Header message_quit(MessageInit_FromServer, ICMServerQuit);
		for (const int i : remoteHostIndices) {
			const CHost host(Hosts[i].Host, Hosts[i].Port);
			// Spew out 5 and trust in God that they arrive
			for (int n = 0; n < 5; ++n) {
				NetworkSendICMessage_Log(NetworkFildes, host, message_quit);
			}
			Hosts[i].Clear();
		}
		NetPlayers = 0;
		NetMesh = false;
		return false;
	}

	DebugPrint("DONE: All configs acked - Now starting..\n");

	// advertise online that we're in progress
	OnlineContextHandler->startAdvertising(true);

	// Give clients a quick-start kick..
	const CInitMessage_Header message_go(MessageInit_FromServer, ICMGo);
	for (const int i : remoteHostIndices) {
		const CHost host(Hosts[i].Host, Hosts[i].Port);
		NetworkSendICMessage_Log(NetworkFildes, host, message_go);
	}
	return true;
}

/**
** Setup the IP-Address of the network server to connect to
**
** @param serveraddr the serveraddress the user has entered
**
** @return True, if error; otherwise false.
*/
int NetworkSetupServerAddress(const std::string &serveraddr, int port)
{
	if (port == 0) {
		port = CNetworkParameter::Instance.defaultPort;
	}
	CHost host(serveraddr, port);
	if (host.isValid() == false) {
		return 1;
	}
	Client.SetServerHost(host);
#ifdef DEBUG
	const std::string hostStr = host.toString();
	DebugPrint("SELECTED SERVER: %s [%s]\n", hostStr.c_str(), serveraddr.c_str());
#endif
	return 0;
}

/**
** Terminate and detach Network connect state machine for the client
*/
void NetworkDetachFromServer()
{
	Client.DetachFromServer();
}

/**
** Setup Network connect state machine for the server
*/
void NetworkInitServerConnect(int openslots)
{
	NetConnectRunning = 1;
	NetConnectType = 1;
	NetMesh = false;

	for (int i = 0; i < PlayerMax; ++i) {
		Hosts[i].Clear();
	}
	ServerSetupState.Clear();
	LocalSetupState.Clear(); // Unused when we are server
	Server.Init(Parameters::Instance.LocalPlayerName, &NetworkFildes, &ServerSetupState);

	// preset the server (initially always slot 0)
	NetLocalHostsSlot = 0;
	Hosts[0].SetName(Parameters::Instance.LocalPlayerName.c_str());

	bool assignedHostPlayer = false;
	for (int i = 0; i < PlayerMax; i++) {
		if (Map.Info.PlayerType[i] == PlayerTypes::PlayerPerson) {
			if (!assignedHostPlayer) {
				assignedHostPlayer = true;
				Hosts[0].PlyNr = i;
			}
			ServerSetupState.CompOpt[i] = SlotOption::Available;
		} else if (Map.Info.PlayerType[i] == PlayerTypes::PlayerNobody) {
			ServerSetupState.CompOpt[i] = SlotOption::Closed;
		} else {
			ServerSetupState.CompOpt[i] = SlotOption::Computer;
		}
	}
}

/**
** Server: ask for the mesh in the games started from now on.
** The game runs as a mesh only if every client reached every other client at the start.
**
** @param mesh  true: every device sends to every device; false: all goes over the server.
*/
void SetNetMesh(bool mesh)
{
	NetMeshWanted = mesh;
}

bool GetNetMesh()
{
	return NetMeshWanted;
}

/**
** Client: a peer answered our probe.
**
** @param hostIndex  host slot of the peer.
*/
void NetworkMeshProbeAnswered(int hostIndex)
{
	if (0 <= hostIndex && hostIndex < PlayerMax) {
		NetMeshProbeMask |= 1 << hostIndex;
	}
}

//
// Test hooks (network_test_hooks.h). The game does not call these functions.
//

void NetworkTestSetClientState(int state)
{
	Client.TestSetState(state);
}

uint16_t NetworkTestMeshProbeMask()
{
	return NetMeshProbeMask;
}

std::vector<std::pair<int, int>> NetworkTestMeshProbeFailed()
{
	return NetMeshProbeFailed;
}

/**
** Notify state change by menu user to connected clients
*/
void NetworkServerResyncClients()
{
	if (NetConnectRunning == 1) {
		Server.MarkClientsAsResync();
	}
}

/**
** Server: whether all connected clients are in sync with the present setup
*/
bool NetworkServerClientsSynced()
{
	return NetConnectRunning == 1 && Server.AllClientsSynced();
}

/**
** Multiplayer network game final copy of server settings to game settings.
*/
void NetworkGamePrepareGameSettings()
{
	DebugPrint("NetPlayers = %d\n", NetPlayers);
	GameSettings = ServerSetupState.ServerGameSettings;
	GameSettings.NetGameType = NetGameTypes::SettingsMultiPlayerGame;

	printf("FINAL NETWORK GAME SETUP\n");
	ServerSetupState.Save(+[](std::string f) { printf("%s", f.c_str()); });

	printf("FINAL NETWORK GAME SETTINGS\n");
	GameSettings.Save(+[](std::string f) { printf("%s\n", f.c_str()); });
}

/**
**  Controls Randomization of Player position in Multiplayer mode.
**  Without arguments, disables randomization. Otherwise, sets the
**  NoRandomization flag to the boolean argument value.
**
**  @param l  Lua state.
*/
static int CclNoRandomPlacementMultiplayer(lua_State *l)
{
	int nargs = lua_gettop(l);
	if (nargs > 1) {
		LuaError(l, "incorrect argument");
	}
	bool flag = nargs ? LuaToBoolean(l, 1) : false;
	NoRandomPlacementMultiplayer = flag;
	return 0;
}

/**
**  Return if player positions in multiplayer mode are randomized.
**
**  @param l  Lua state.
*/
static int CclUsesRandomPlacementMultiplayer(lua_State *l)
{
	LuaCheckArgs(l, 0);
	lua_pushboolean(l, !NoRandomPlacementMultiplayer);
	return 1;
}

static int CclNetworkDiscoverServers(lua_State *l)
{
	LuaCheckArgs(l, 1);
	bool start = LuaToBoolean(l, 1);

	auto callback = [l](char* ip) {
		auto i = lua_objlen(l, -1) + 1;
		lua_pushnumber(l, i);
		lua_pushstring(l, ip);
		lua_settable(l, -3);
	};

	lua_newtable(l);
	if (start) {
		MdnsService.QueryServers(callback);
	}

	return 1;
}


/**
**  The wait as the scripts are told it.
**
**  @param state   The wait state of the network game.
**  @param nameOf  Gives the name of a player number.
*/
CNetWaitView NetworkBuildWaitView(const CNetworkWaitState &state,
								  const std::function<std::string(int)> &nameOf)
{
	CNetWaitView view;

	view.Held = state.Held;
	view.SelfLoaded = state.Loaded;
	view.SelfOut = state.SelfOut;
	view.Paused = state.Paused;
	if (state.Paused) {
		view.PausedBy = state.PausedBy;
		view.PausedByName = nameOf(state.PausedBy);
	}
	if (state.SelfOut) {
		switch (static_cast<mesh::Reason>(state.SelfOutReason)) {
			case mesh::Reason::Isolated: view.SelfOutReason = "isolated"; break;
			case mesh::Reason::Desync: view.SelfOutReason = "desync"; break;
			default: view.SelfOutReason = "connection_lost"; break;
		}
	}
	for (int p = 0; p < static_cast<int>(state.MsLeftOf.size()); ++p) {
		if (state.RemovedAtStart & (1u << p)) {
			view.RemovedAtStart.push_back(p);
		}
	}
	if (state.Held) {
		view.Phase = "start";
		for (int p = 0; p < static_cast<int>(state.MsLeftOf.size()); ++p) {
			if (!(state.NotLoaded & (1u << p))) {
				continue;
			}
			CNetWaitPlayer player;
			player.Player = p;
			player.Name = nameOf(p);
			player.State = (state.NoSignal & (1u << p)) ? "silent" : "loading";
			player.MsLeft = state.MsLeftOf[p];
			view.Players.push_back(player);
		}
		return view;
	}
	if (state.Player != -1) {
		view.Phase = "game";
		view.Player = state.Player;
		view.Name = nameOf(state.Player);
		view.MsLeft = state.MsLeft;
		view.AllowanceMsLeft = state.AllowanceMsLeft;
		view.Removing = state.Removing;
		CNetWaitPlayer player;
		player.Player = state.Player;
		player.Name = view.Name;
		player.State = state.Removing ? "removing" : "silent";
		player.MsLeft = state.MsLeft;
		view.Players.push_back(player);
	} else if (state.SelfOut) {
		view.Phase = "game";
	}
	return view;
}

/// Debug: the made-up wait, and when it was set
static CNetWaitView FakeWaitView;
static bool FakeWaitViewSet = false;
static uint32_t FakeWaitViewTicks = 0;
/// When this device was first seen out of the game, 0 = it is not
static uint32_t SelfOutTicks = 0;

void NetworkSetFakeWaitView(const CNetWaitView *view)
{
	FakeWaitViewSet = view != nullptr;
	if (view) {
		FakeWaitView = *view;
		FakeWaitView.Fake = true;
		FakeWaitViewTicks = SDL_GetTicks();
	}
}

bool NetworkWaitViewFaked()
{
	return FakeWaitViewSet;
}

CNetWaitView NetworkGetWaitView()
{
	if (FakeWaitViewSet) {
		// Only what the scripts are told is made up: it counts down as the real one does.
		CNetWaitView view = FakeWaitView;
		const int passed = static_cast<int>(SDL_GetTicks() - FakeWaitViewTicks);
		view.MsLeft = std::max(0, view.MsLeft - passed);
		for (CNetWaitPlayer &player : view.Players) {
			if (player.MsLeft >= 0) {
				player.MsLeft = std::max(0, player.MsLeft - passed);
			}
		}
		if (view.SelfOut) {
			view.SelfOutMs += passed;
		}
		return view;
	}
	CNetWaitView view = NetworkBuildWaitView(NetworkGetWaitState(), [](int player) {
		return (player >= 0 && player < PlayerMax) ? Players[player].Name : std::string();
	});
	if (view.SelfOut) {
		const uint32_t now = SDL_GetTicks();
		if (SelfOutTicks == 0) {
			SelfOutTicks = now ? now : 1;
		}
		view.SelfOutMs = static_cast<int>(now - SelfOutTicks);
	} else {
		SelfOutTicks = 0;
	}
	return view;
}

/// The scripts have a NetWaitFrame function: they show the wait
static bool WaitScripted = false;

bool NetworkWaitScripted()
{
	return WaitScripted;
}

/**
**  Once per drawn frame of a game: the scripts follow the wait (Lua NetWaitFrame).
**  Nothing is called in a game without a network and without a made-up wait.
*/
void NetworkWaitFrame()
{
	if (!IsNetworkGame() && !FakeWaitViewSet && !WaitScripted) {
		return;
	}
	lua_getglobal(Lua, "NetWaitFrame");
	if (!lua_isfunction(Lua, -1)) {
		lua_pop(Lua, 1);
		WaitScripted = false;
		return;
	}
	// The call after the last frame of a made-up wait lets the scripts take their plate away.
	WaitScripted = IsNetworkGame() || FakeWaitViewSet;
	LuaCall(Lua, 0, 0, lua_gettop(Lua) - 1, false);
}

/**
**  Server: ask for the mesh in the games started from now on.
**
**  @param l  Lua state: one boolean.
*/
static int CclSetNetMesh(lua_State *l)
{
	LuaCheckArgs(l, 1);
	SetNetMesh(LuaToBoolean(l, 1));
	return 0;
}

/**
**  Does this game run as a mesh? The answer after the probe of the start.
*/
static int CclNetIsMesh(lua_State *l)
{
	LuaCheckArgs(l, 0);
	lua_pushboolean(l, NetMesh);
	return 1;
}

/**
**  Mesh: the bounds in seconds for the games started from now on (drop, load); 0 = the default.
*/
static int CclSetNetTimeouts(lua_State *l)
{
	LuaCheckArgs(l, 2);
	NetworkSetMeshTimeouts(LuaToNumber(l, 1), LuaToNumber(l, 2));
	return 0;
}

static void PushWaitPlayer(lua_State *l, const CNetWaitPlayer &player)
{
	lua_newtable(l);
	lua_pushnumber(l, player.Player);
	lua_setfield(l, -2, "player");
	lua_pushstring(l, player.Name.c_str());
	lua_setfield(l, -2, "name");
	lua_pushstring(l, player.State.c_str());
	lua_setfield(l, -2, "state");
	lua_pushnumber(l, player.MsLeft);
	lua_setfield(l, -2, "msLeft");
}

/**
**  Who the game waits for, as a table (see CNetWaitView).
*/
static int CclNetWaitState(lua_State *l)
{
	LuaCheckArgs(l, 0);
	const CNetWaitView view = NetworkGetWaitView();

	lua_newtable(l);
	lua_pushstring(l, view.Phase.c_str());
	lua_setfield(l, -2, "phase");
	lua_pushboolean(l, view.Held);
	lua_setfield(l, -2, "held");
	lua_pushboolean(l, view.SelfLoaded);
	lua_setfield(l, -2, "selfLoaded");
	lua_pushboolean(l, view.SelfOut);
	lua_setfield(l, -2, "selfOut");
	if (view.SelfOut) {
		lua_pushstring(l, view.SelfOutReason.c_str());
		lua_setfield(l, -2, "selfOutReason");
	}
	lua_pushnumber(l, view.SelfOutMs);
	lua_setfield(l, -2, "selfOutMs");
	if (view.Player != -1 || !view.Name.empty()) {
		lua_pushnumber(l, view.Player);
		lua_setfield(l, -2, "player");
		lua_pushstring(l, view.Name.c_str());
		lua_setfield(l, -2, "name");
	}
	lua_pushnumber(l, view.MsLeft);
	lua_setfield(l, -2, "msLeft");
	lua_pushnumber(l, view.AllowanceMsLeft);
	lua_setfield(l, -2, "allowanceMsLeft");
	lua_pushboolean(l, view.Removing);
	lua_setfield(l, -2, "removing");
	lua_pushboolean(l, view.Paused);
	lua_setfield(l, -2, "paused");
	if (view.Paused) {
		lua_pushnumber(l, view.PausedBy);
		lua_setfield(l, -2, "pausedBy");
		lua_pushstring(l, view.PausedByName.c_str());
		lua_setfield(l, -2, "pausedByName");
	}
	lua_newtable(l);
	for (size_t i = 0; i != view.Players.size(); ++i) {
		PushWaitPlayer(l, view.Players[i]);
		lua_rawseti(l, -2, static_cast<int>(i) + 1);
	}
	lua_setfield(l, -2, "players");
	lua_newtable(l);
	for (size_t i = 0; i != view.RemovedAtStart.size(); ++i) {
		lua_pushnumber(l, view.RemovedAtStart[i]);
		lua_rawseti(l, -2, static_cast<int>(i) + 1);
	}
	lua_setfield(l, -2, "removedAtStart");
	lua_pushboolean(l, view.Fake);
	lua_setfield(l, -2, "fake");
	return 1;
}

/**
**  The local player presses Pause in a network game: the game is paused for everybody,
**  or a paused game goes on for everybody. False if nothing was done (no mesh game,
**  the game has not begun, this device is out).
*/
static int CclNetTogglePause(lua_State *l)
{
	LuaCheckArgs(l, 0);
	lua_pushboolean(l, NetworkTogglePause());
	return 1;
}

/**
**  Why a player is out of the game: 0 he left, 1 the connection to him was lost,
**  2 he never started; nil while he has not quit.
*/
static int CclNetQuitReason(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int reason = NetworkGetQuitReason(LuaToNumber(l, 1));

	if (reason < 0) {
		lua_pushnil(l);
	} else {
		lua_pushnumber(l, reason);
	}
	return 1;
}

/**
**  Whether this device has a network link at all: an interface that is up and has an address.
**  False on a device whose Wi-Fi is off or gone - then it is this device that is cut off, not
**  the players it cannot hear.
*/
static int CclNetHasLink(lua_State *l)
{
	LuaCheckArgs(l, 0);
	lua_pushboolean(l, !NetSocketAddr().empty());
	return 1;
}

void NetworkCclRegister()
{
	lua_register(Lua, "NetHasLink", CclNetHasLink);
	lua_register(Lua, "SetNetMesh", CclSetNetMesh);
	lua_register(Lua, "NetIsMesh", CclNetIsMesh);
	lua_register(Lua, "SetNetTimeouts", CclSetNetTimeouts);
	lua_register(Lua, "NetWaitState", CclNetWaitState);
	lua_register(Lua, "NetQuitReason", CclNetQuitReason);
	lua_register(Lua, "NetTogglePause", CclNetTogglePause);
	lua_register(Lua, "SetNetWaitPlate", CclSetNetWaitPlate);
	lua_register(Lua, "NoRandomPlacementMultiplayer", CclNoRandomPlacementMultiplayer);
	lua_register(Lua, "UsesRandomPlacementMultiplayer", CclUsesRandomPlacementMultiplayer);
	lua_register(Lua, "NetworkDiscoverServers", CclNetworkDiscoverServers);
}


//@}
