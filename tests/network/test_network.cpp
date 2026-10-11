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
/**@name test_net_lowlevel.cpp - The test file for net_lowlevel.cpp. */
//
//      (c) Copyright 2013 by Joris Dauphin
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

#include <doctest.h>

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
#include "online_service.h"
#include "player.h"
#include "unit.h"
#include "video.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

void FillCustomValue(CNetworkCommand *obj)
{
	obj->Dest = 0x1234;
	obj->Unit = 0x5678;
	obj->X = 0x9ABC;
	obj->Y = 0xDEF0;
}

void FillCustomValue(CNetworkExtendedCommand *obj)
{
	obj->ExtendedType = 11;
	obj->Arg1 = 22;
	obj->Arg2 = 0x1234;
	obj->Arg3 = 0x5678;
	obj->Arg4 = 0x9ABC;
}

void FillCustomValue(CNetworkChat *obj)
{
	obj->Text = "abcdefghijklmnopqrstuvwxyz";
}
void FillCustomValue(CNetworkCommandSync *obj)
{
	obj->syncSeed = 0x01234567;
	obj->syncHash = 0x89ABCDEF;
}
void FillCustomValue(CNetworkCommandQuit *obj)
{
	obj->player = 0x0123;
	obj->reason = 0x45;
}
void FillCustomValue(CNetworkSelection *obj)
{
	for (int i = 0; i != 10; ++i) {
		obj->Units.push_back(0x0123 * i);
	}
}

void FillCustomValue(CNetworkPacketHeader *obj)
{
	obj->Cycle = 42;
	for (int i = 0; i != MaxNetworkCommands; ++i) {
		obj->Type[i] = 0x05 + i * 0x12;
	}
}

template <typename T>
bool Comp(const T &lhs, const T &rhs)
{
	return memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

bool Comp(const CNetworkChat &lhs, const CNetworkChat &rhs)
{
	return lhs.Text == rhs.Text;
}

bool Comp(const CNetworkSelection &lhs, const CNetworkSelection &rhs)
{
	return lhs.Units == rhs.Units;
}


template <typename T>
bool CheckSerialization()
{
	T obj1;

	FillCustomValue(&obj1);
	std::vector<unsigned char> buffer(obj1.Size());
	obj1.Serialize(buffer.data());

	T obj2;
	obj2.Deserialize(buffer.data());
	bool res = Comp(obj1, obj2);
	return res;
}

TEST_CASE("CNetworkCommand")
{
	CHECK(CheckSerialization<CNetworkCommand>());
}
TEST_CASE("CNetworkExtendedCommand")
{
	CHECK(CheckSerialization<CNetworkExtendedCommand>());
}
TEST_CASE("CNetworkChat")
{
	CHECK(CheckSerialization<CNetworkChat>());
}
TEST_CASE("CNetworkCommandSync")
{
	CHECK(CheckSerialization<CNetworkCommandSync>());
}
TEST_CASE("CNetworkCommandQuit")
{
	CHECK(CheckSerialization<CNetworkCommandQuit>());
}
TEST_CASE("CNetworkSelection")
{
	CHECK(CheckSerialization<CNetworkSelection>());
}
TEST_CASE("CNetworkPacketHeader")
{
	CHECK(CheckSerialization<CNetworkPacketHeader>());
}
//TEST_CASE("CNetworkPacket")

//
// In-game packets: hand-built packets through the parser of network.cpp.
//

namespace {

const int ServerPlayer = 0; // host slot 0
const int LocalPlayer = 1;  // host slot 1
const int OtherPlayer = 2;  // host slot 2

class NetworkInit
{
public:
	NetworkInit() { NetInit(); }
	~NetworkInit() { NetExit(); }
};

/**
**  A running game of three devices, seen from the client of player 1.
**  With sockets the local port and the addresses of the two others are real.
*/
class InGameNetwork : public NetworkInit
{
public:
	explicit InGameNetwork(bool withSockets = false) :
		serverHost("127.0.0.1", 6511),
		localHost("127.0.0.1", 6512),
		otherHost("127.0.0.1", 6513),
		unknownHost("127.0.0.1", 6514),
		savedThisPlayer(ThisPlayer),
		savedNumPlayers(NumPlayers),
		savedTimeout(CNetworkParameter::Instance.timeoutInS)
	{
		if (withSockets) {
			serverSocket.Open(serverHost);
			otherSocket.Open(otherHost);
			NetworkFildes.Open(localHost);
		}
		for (int i = 0; i != PlayerMax; ++i) {
			Hosts[i].Clear();
			Players[i].Index = i;
		}
		SetHost(0, ServerPlayer, serverHost, "server");
		SetHost(1, LocalPlayer, localHost, "local");
		SetHost(2, OtherPlayer, otherHost, "other");
		NetPlayers = 3;
		NetConnectType = 2;
		NetLocalHostsSlot = 1;
		NumPlayers = 3;
		ThisPlayer = &Players[LocalPlayer];
		GameCycle = 0;
		NetworkOnStartGame(); // cycles 0..lag are filled for the three players
		GameCycle = 20;
	}

	~InGameNetwork()
	{
		NetworkFildes.Close();
		serverSocket.Close();
		otherSocket.Close();
		for (int i = 0; i != PlayerMax; ++i) {
			Hosts[i].Clear();
		}
		NetPlayers = 0;
		NetConnectType = 0;
		NetLocalHostsSlot = 0;
		NetMesh = false;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
		GameCycle = 0;
		NetworkInSync = true;
		CNetworkParameter::Instance.timeoutInS = savedTimeout;
	}

	static void SetHost(int hostIndex, int playerIndex, const CHost &host, const char *name)
	{
		Hosts[hostIndex].Host = host.getIp();
		Hosts[hostIndex].Port = host.getPort();
		Hosts[hostIndex].PlyNr = playerIndex;
		Hosts[hostIndex].SetName(name);
	}

	/// Receive one in-game packet on a socket, false if nothing arrives
	static bool Receive(CUDPSocket &socket, CNetworkPacket &packet, int &commands)
	{
		if (!socket.HasDataToRead(1000)) {
			return false;
		}
		unsigned char buf[1024];
		CHost from;
		const int len = socket.Recv(buf, sizeof(buf), &from);
		if (len < int(CNetworkPacketHeader::Size())) {
			return false;
		}
		packet.Deserialize(buf, len, &commands);
		return true;
	}

public:
	const CHost serverHost;
	const CHost localHost;
	const CHost otherHost;
	const CHost unknownHost;
	CUDPSocket serverSocket;
	CUDPSocket otherSocket;
private:
	CPlayer *savedThisPlayer;
	int savedNumPlayers;
	unsigned int savedTimeout;
};

class InGameNetworkWithSockets : public InGameNetwork
{
public:
	InGameNetworkWithSockets() : InGameNetwork(true) {}
};

std::vector<unsigned char> BuildPacket(int origPlayer, unsigned long cycle, unsigned char type,
                                       const std::vector<unsigned char> &data)
{
	CNetworkPacket packet;

	packet.Header.Cycle = cycle & 0xFF;
	packet.Header.OrigPlayer = origPlayer;
	packet.Header.Type[0] = type;
	packet.Command[0] = data;
	std::vector<unsigned char> buf(packet.Size(1));
	packet.Serialize(buf.data(), 1);
	return buf;
}

std::vector<unsigned char> SyncPacket(int origPlayer, unsigned long cycle, uint32_t seed)
{
	CNetworkCommandSync nc;
	nc.syncSeed = seed;
	nc.syncHash = ~seed;
	std::vector<unsigned char> data(nc.Size());
	nc.Serialize(data.data());
	return BuildPacket(origPlayer, cycle, MessageSync, data);
}

std::vector<unsigned char> QuitPacket(int origPlayer, unsigned long cycle, int quitPlayer)
{
	CNetworkCommandQuit nc;
	nc.player = quitPlayer;
	std::vector<unsigned char> data(nc.Size());
	nc.Serialize(data.data());
	return BuildPacket(origPlayer, cycle, MessageQuit, data);
}

void Parse(const std::vector<unsigned char> &buf, const CHost &host)
{
	NetworkTestParseInGameEvent(buf.data(), int(buf.size()), host);
}

/// Seed of the sync command stored for a player and a cycle, 0 if there is none
uint32_t StoredSeed(unsigned long cycle, int player)
{
	unsigned char type = MessageNone;
	std::vector<unsigned char> data;

	if (!NetworkTestGetCommand(cycle, player, 0, type, data) || type != MessageSync) {
		return 0;
	}
	CNetworkCommandSync nc;
	nc.Deserialize(data.data());
	return nc.syncSeed;
}

bool HasStored(unsigned long cycle, int player)
{
	unsigned char type = MessageNone;
	std::vector<unsigned char> data;

	return NetworkTestGetCommand(cycle, player, 0, type, data);
}

}

TEST_CASE_FIXTURE(InGameNetwork, "in-game packet: the first packet of a player for a cycle wins")
{
	// forwarded by the server
	Parse(SyncPacket(OtherPlayer, 25, 0x1111), serverHost);
	CHECK(StoredSeed(25, OtherPlayer) == 0x1111);

	// the same packet again, and another one for the same player and cycle
	Parse(SyncPacket(OtherPlayer, 25, 0x1111), serverHost);
	Parse(SyncPacket(OtherPlayer, 25, 0x2222), serverHost);
	Parse(QuitPacket(OtherPlayer, 25, OtherPlayer), serverHost);
	CHECK(StoredSeed(25, OtherPlayer) == 0x1111);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);

	// other cycles and other players are not touched
	CHECK(HasStored(25, ServerPlayer) == false);
	Parse(SyncPacket(OtherPlayer, 26, 0x3333), serverHost);
	CHECK(StoredSeed(26, OtherPlayer) == 0x3333);
}

TEST_CASE_FIXTURE(InGameNetwork, "in-game packet: taken only from the address of the player or from the server")
{
	// an address nobody plays at
	Parse(SyncPacket(OtherPlayer, 25, 0x1111), unknownHost);
	Parse(SyncPacket(255, 25, 0x1111), unknownHost);
	CHECK(HasStored(25, OtherPlayer) == false);
	CHECK(HasStored(25, ServerPlayer) == false);

	// a client names the server or the local player
	Parse(SyncPacket(ServerPlayer, 25, 0x2222), otherHost);
	Parse(SyncPacket(LocalPlayer, 25, 0x2222), otherHost);
	CHECK(HasStored(25, ServerPlayer) == false);
	CHECK(HasStored(25, LocalPlayer) == false);

	// a player number outside the table
	Parse(SyncPacket(200, 25, 0x2222), serverHost);

	// the player itself, with and without its number in the header
	Parse(SyncPacket(OtherPlayer, 25, 0x3333), otherHost);
	CHECK(StoredSeed(25, OtherPlayer) == 0x3333);
	Parse(SyncPacket(255, 26, 0x4444), otherHost);
	CHECK(StoredSeed(26, OtherPlayer) == 0x4444);

	// the server for itself and for another player
	Parse(SyncPacket(ServerPlayer, 25, 0x5555), serverHost);
	CHECK(StoredSeed(25, ServerPlayer) == 0x5555);
	Parse(SyncPacket(OtherPlayer, 27, 0x6666), serverHost);
	CHECK(StoredSeed(27, OtherPlayer) == 0x6666);
}

TEST_CASE_FIXTURE(InGameNetwork, "in-game packet: on the server nobody sends for another player")
{
	NetConnectType = 1;
	NetLocalHostsSlot = 0;
	ThisPlayer = &Players[ServerPlayer];

	// the client of player 2 names player 1: not stored, not passed on
	Parse(SyncPacket(LocalPlayer, 25, 0x1111), otherHost);
	CHECK(HasStored(25, LocalPlayer) == false);
	Parse(QuitPacket(LocalPlayer, 25, LocalPlayer), otherHost);
	CHECK(HasStored(25, LocalPlayer) == false);
	CHECK(NetworkTestPlayerQuit(LocalPlayer) == false);
}

TEST_CASE_FIXTURE(InGameNetwork, "in-game packet: a cycle already executed is dropped")
{
	// GameCycle is 20: it has been executed
	Parse(SyncPacket(OtherPlayer, 15, 0x1111), serverHost);
	Parse(SyncPacket(OtherPlayer, 20, 0x2222), serverHost);
	CHECK(HasStored(15, OtherPlayer) == false);
	CHECK(HasStored(20, OtherPlayer) == false);

	Parse(QuitPacket(OtherPlayer, 19, OtherPlayer), serverHost);
	CHECK(HasStored(19, OtherPlayer) == false);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);

	Parse(SyncPacket(OtherPlayer, 21, 0x3333), serverHost);
	CHECK(StoredSeed(21, OtherPlayer) == 0x3333);
}

TEST_CASE_FIXTURE(InGameNetwork, "in-game packet: a quit goes to the player it names")
{
	// the server tells that player 2 has quit (time-out, or the answer to a resend request)
	Parse(QuitPacket(OtherPlayer, 21, OtherPlayer), serverHost);

	unsigned char type = MessageNone;
	std::vector<unsigned char> data;
	REQUIRE(NetworkTestGetCommand(21, OtherPlayer, 0, type, data));
	CHECK(type == MessageQuit);
	CNetworkCommandQuit nc;
	REQUIRE(data.size() == nc.Size());
	nc.Deserialize(data.data());
	CHECK(nc.player == OtherPlayer);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == true);

	// the commands of the server for that cycle are still awaited, and then taken
	CHECK(HasStored(21, ServerPlayer) == false);
	CHECK(NetworkTestPlayerQuit(ServerPlayer) == false);
	Parse(SyncPacket(ServerPlayer, 21, 0x1111), serverHost);
	CHECK(StoredSeed(21, ServerPlayer) == 0x1111);
}

TEST_CASE_FIXTURE(InGameNetworkWithSockets, "in-game packet: a resend request is answered to the asker only")
{
	REQUIRE(NetworkFildes.IsValid());
	REQUIRE(serverSocket.IsValid());
	REQUIRE(otherSocket.IsValid());

	// cycle 6 of the local player is one of the cycles filled at the start
	GameCycle = 5;
	REQUIRE(HasStored(6, LocalPlayer));

	Parse(BuildPacket(255, 6, MessageResend, {}), otherHost);

	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(otherSocket, packet, commands));
	CHECK(commands == 1);
	CHECK(packet.Header.Type[0] == MessageSync);
	CHECK(packet.Header.Cycle == 6);
	CHECK(packet.Header.OrigPlayer == LocalPlayer);
	CHECK(serverSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(InGameNetworkWithSockets, "in-game packet: the server answers a resend request with the quit under the name of the player who quit")
{
	REQUIRE(NetworkFildes.IsValid());
	REQUIRE(serverSocket.IsValid());
	REQUIRE(otherSocket.IsValid());

	// we are the server here; the two sockets are the clients of player 1 and player 2
	NetConnectType = 1;
	NetLocalHostsSlot = 0;
	ThisPlayer = &Players[ServerPlayer];
	SetHost(0, ServerPlayer, localHost, "server");
	SetHost(1, LocalPlayer, serverHost, "client");
	CUDPSocket &clientSocket = serverSocket;
	const CHost &clientHost = serverHost;
	GameCycle = 5;

	// player 2 was heard and then timed out: its quit is in cycle 6
	Parse(SyncPacket(OtherPlayer, 12, 0x1111), otherHost);
	NetworkTestTimeOutPlayer(OtherPlayer);
	CNetworkPacket packet;
	int commands = 0;
	// to the client of player 1: the packet of player 2 passed on, then the time-out packet
	REQUIRE(Receive(clientSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageSync);
	CHECK(packet.Header.OrigPlayer == OtherPlayer);
	REQUIRE(Receive(clientSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageQuit);
	CHECK(packet.Header.OrigPlayer == OtherPlayer);
	CHECK(packet.Command[0].size() == CNetworkCommandQuit::Size());
	// nothing to the player timed out
	CHECK(otherSocket.HasDataToRead(200) == false);

	// the client of player 1 asks for cycle 6
	Parse(BuildPacket(255, 6, MessageResend, {}), clientHost);
	REQUIRE(Receive(clientSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageSync);
	CHECK(packet.Header.Cycle == 6);
	CHECK(packet.Header.OrigPlayer == ServerPlayer);
	REQUIRE(Receive(clientSocket, packet, commands));
	CHECK(commands == 1);
	CHECK(packet.Header.Type[0] == MessageQuit);
	CHECK(packet.Header.Cycle == 6);
	CHECK(packet.Header.OrigPlayer == OtherPlayer);
	CNetworkCommandQuit nc;
	REQUIRE(packet.Command[0].size() == nc.Size());
	nc.Deserialize(packet.Command[0].data());
	CHECK(nc.player == OtherPlayer);
	CHECK(clientSocket.HasDataToRead(200) == false);
	// the other client gets the request passed on, no answer
	REQUIRE(Receive(otherSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageResend);
	CHECK(otherSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(InGameNetworkWithSockets, "in-game packet: the time-out packet carries the quit and names the player")
{
	REQUIRE(NetworkFildes.IsValid());
	REQUIRE(serverSocket.IsValid());

	// player 2 has been heard once; the server never (no time-out for it)
	Parse(SyncPacket(OtherPlayer, 22, 0x1111), serverHost);
	NetworkTestTimeOutPlayer(OtherPlayer);

	// the quit is in the slot of player 2 of the next cycle
	unsigned char type = MessageNone;
	std::vector<unsigned char> data;
	REQUIRE(NetworkTestGetCommand(21, OtherPlayer, 0, type, data));
	CHECK(type == MessageQuit);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == true);

	// a client sends to the server only
	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(serverSocket, packet, commands));
	CHECK(commands == 1);
	CHECK(packet.Header.Type[0] == MessageQuit);
	CHECK(packet.Header.Cycle == 21);
	CHECK(packet.Header.OrigPlayer == OtherPlayer);
	CNetworkCommandQuit nc;
	REQUIRE(packet.Command[0].size() == nc.Size());
	nc.Deserialize(packet.Command[0].data());
	CHECK(nc.player == OtherPlayer);
	CHECK(serverSocket.HasDataToRead(200) == false);
	CHECK(otherSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(InGameNetworkWithSockets, "in-game packet: the time-out counts real time, not frames")
{
	REQUIRE(NetworkFildes.IsValid());
	REQUIRE(serverSocket.IsValid());

	// player 2 has just been heard; then a great number of frames is drawn in no time
	Parse(SyncPacket(OtherPlayer, 22, 0x1111), serverHost);
	const unsigned long savedFrameCounter = FrameCounter;
	FrameCounter += 1000000;
	NetworkRecover();
	FrameCounter = savedFrameCounter;

	CHECK(HasStored(21, OtherPlayer) == false);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);
	// only the resend request went out
	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(serverSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageResend);
	CHECK(packet.Header.OrigPlayer == 255);
	CHECK(serverSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(NetworkInit, "server start: a client which does not answer ends the wait")
{
	const CHost localHost("127.0.0.1", 6515);
	const CHost clientHost("127.0.0.1", 6516);
	CUDPSocket clientSocket;
	clientSocket.Open(clientHost);
	NetworkFildes.Open(localHost);
	REQUIRE(clientSocket.IsValid());
	REQUIRE(NetworkFildes.IsValid());

	PlayerTypes savedPlayerType[PlayerMax];
	for (int i = 0; i != PlayerMax; ++i) {
		savedPlayerType[i] = Map.Info.PlayerType[i];
		Map.Info.PlayerType[i] = i < 2 ? PlayerTypes::PlayerPerson : PlayerTypes::PlayerNobody;
	}
	NetworkInitServerConnect(2);
	CHECK(NetLocalHostsSlot == 0);
	Hosts[0].SetName("host"); // the test has no local player name, and a host without name and number is no host
	// a client which has joined and then went silent
	Hosts[1].Host = clientHost.getIp();
	Hosts[1].Port = clientHost.getPort();
	Hosts[1].PlyNr = 1;
	Hosts[1].SetName("silent");

	const unsigned long start = GetTicks();
	CHECK(NetworkServerStartGame() == false);
	const unsigned long waited = GetTicks() - start;
	CHECK(waited >= 15000);
	CHECK(waited < 20000);
	CHECK(Hosts[0].IsValid() == true);
	CHECK(Hosts[1].IsValid() == false);
	CHECK(NetPlayers == 0);

	// the client was sent the config all the time and the quit of the server at the end
	int configs = 0;
	int quits = 0;
	while (clientSocket.HasDataToRead(100)) {
		unsigned char buf[4096];
		CHost from;
		const int len = clientSocket.Recv(buf, sizeof(buf), &from);
		if (len < int(CInitMessage_Header::Size())) {
			break;
		}
		CInitMessage_Header header;
		header.Deserialize(buf);
		if (header.GetType() == MessageInit_FromServer && header.GetSubType() == ICMConfig) {
			CHECK(len == int(CInitMessage_Config::Size()));
			++configs;
		} else if (header.GetType() == MessageInit_FromServer && header.GetSubType() == ICMServerQuit) {
			++quits;
		}
	}
	CHECK(configs >= 10);
	CHECK(quits == 5);

	NetworkFildes.Close();
	clientSocket.Close();
	NetConnectRunning = 0;
	NetConnectType = 0;
	for (int i = 0; i != PlayerMax; ++i) {
		Hosts[i].Clear();
		Map.Info.PlayerType[i] = savedPlayerType[i];
	}
	ServerSetupState.Clear();
	LocalSetupState.Clear();
}

//
// Mesh (every device sends to every device). The network state of the engine is global to the
// process, so one real engine endpoint is driven here and plain sockets play the other devices.
//

namespace {

class MeshNetwork : public InGameNetworkWithSockets
{
public:
	MeshNetwork()
	{
		NetMesh = true;
		NetworkTestTakeMeshMessages();
		droppedAtStart = NetworkTestMeshDropped();
	}

	/// we are the server; serverSocket and otherSocket are the clients of player 1 and player 2
	void BecomeServer()
	{
		NetConnectType = 1;
		NetLocalHostsSlot = 0;
		ThisPlayer = &Players[ServerPlayer];
		SetHost(0, ServerPlayer, localHost, "server");
		SetHost(1, LocalPlayer, serverHost, "client");
	}

	unsigned int Dropped() const { return NetworkTestMeshDropped() - droppedAtStart; }

	/// Receive one datagram on a socket, empty if nothing arrives
	static std::vector<unsigned char> ReceiveBytes(CUDPSocket &socket, int timeout = 1000)
	{
		if (!socket.HasDataToRead(timeout)) {
			return {};
		}
		unsigned char buf[4096];
		CHost from;
		const int len = socket.Recv(buf, sizeof(buf), &from);
		if (len <= 0) {
			return {};
		}
		return std::vector<unsigned char>(buf, buf + len);
	}

	/// Let the engine read the datagram waiting on its socket, false if none arrives
	static bool EngineReads()
	{
		if (!NetworkFildes.HasDataToRead(1000)) {
			return false;
		}
		NetworkEvent();
		return true;
	}

	static std::vector<unsigned char> MeshBytes(const mesh::Msg &message, int from)
	{
		unsigned char buf[mesh::kWireMaxSize];
		const size_t size = mesh::Serialize(message, from, buf, sizeof(buf));
		return std::vector<unsigned char>(buf, buf + size);
	}

	static std::vector<unsigned char> ProbeBytes(bool ack, int from)
	{
		unsigned char buf[mesh::kWireProbeSize];
		const size_t size = mesh::SerializeProbe(ack, from, buf, sizeof(buf));
		return std::vector<unsigned char>(buf, buf + size);
	}

	static mesh::Msg NeedMessage(int32_t cycle, uint16_t mask)
	{
		mesh::Msg message;
		message.kind = mesh::Kind::Need;
		message.flags = 1;
		message.cycle = cycle;
		message.mask = mask;
		return message;
	}

private:
	unsigned int droppedAtStart = 0;
};

class StubOnlineContext : public OnlineContext
{
public:
	bool handleUDP(const unsigned char *, int, CHost) override { return false; }
	void doOneStep() override {}
	void joinGame(std::string, std::string) override {}
	void leaveGame() override {}
	void startAdvertising(bool) override {}
	void stopAdvertising() override {}
	void reportGameResult() override {}
};

/**
**  A client of the start hand-shake played by a socket: it answers the config (with the
**  peers it claims to reach, as a real client does after its probes) and the final state.
*/
class FakeClient
{
public:
	FakeClient(const CHost &host, const CHost &serverHost, bool withMask, uint16_t mask) :
		host(host), serverHost(serverHost), withMask(withMask), mask(mask)
	{
		socket.Open(host);
	}

	void Run(const std::atomic<bool> &stop)
	{
		while (!stop) {
			if (!socket.HasDataToRead(20)) {
				// a client repeats its answer by itself until the final state arrives
				if (configs && !states && GetTicks() - lastAck >= 100) {
					SendAck(mask);
				}
				continue;
			}
			unsigned char buf[4096];
			CHost from;
			const int len = socket.Recv(buf, sizeof(buf), &from);
			if (len < int(CInitMessage_Header::Size())) {
				continue;
			}
			CInitMessage_Header header;
			header.Deserialize(buf);
			if (header.GetType() != MessageInit_FromServer) {
				continue;
			}
			if (header.GetSubType() == ICMConfig && len == int(CInitMessage_Config::Size())) {
				config.Deserialize(buf);
				// nobody has answered a probe yet when the first answer goes out
				SendAck(configs ? mask : 0);
				++configs;
			} else if (header.GetSubType() == ICMState && len == int(CInitMessage_State::Size())) {
				CInitMessage_State state;
				state.Deserialize(buf);
				stateMesh = state.mesh;
				++states;
				const CInitMessage_Header go(MessageInit_FromClient, ICMGo);
				NetworkSendICMessage(socket, serverHost, go);
			} else if (header.GetSubType() == ICMGo) {
				go = true;
			}
		}
	}

private:
	void SendAck(uint16_t peers)
	{
		lastAck = GetTicks();
		if (withMask) {
			const std::vector<unsigned char> buf = CInitMessage_ConfigAck(peers).Serialize();
			socket.Send(serverHost, buf.data(), buf.size());
		} else {
			const CInitMessage_Header ack(MessageInit_FromClient, ICMConfig);
			NetworkSendICMessage(socket, serverHost, ack);
		}
	}

public:
	CUDPSocket socket;
	const CHost host;
	const CHost serverHost;
	const bool withMask;
	const uint16_t mask;
	CInitMessage_Config config;
	int configs = 0;
	int states = 0;
	int stateMesh = -1;
	bool go = false;
private:
	unsigned long lastAck = 0;
};

/**
**  The start of a game on the server with two clients played by sockets.
*/
class ServerStart : public NetworkInit
{
public:
	ServerStart(int port, bool withMask, uint16_t mask1, uint16_t mask2) :
		localHost("127.0.0.1", port),
		client1(CHost("127.0.0.1", port + 1), localHost, withMask, mask1),
		client2(CHost("127.0.0.1", port + 2), localHost, withMask, mask2),
		savedRandomPlacement(NoRandomPlacementMultiplayer),
		savedMeshWanted(GetNetMesh())
	{
		NetworkFildes.Open(localHost);
		for (int i = 0; i != PlayerMax; ++i) {
			savedPlayerType[i] = Map.Info.PlayerType[i];
			Map.Info.PlayerType[i] = i < 3 ? PlayerTypes::PlayerPerson : PlayerTypes::PlayerNobody;
		}
		savedOnline = std::move(OnlineContextHandler);
		OnlineContextHandler = std::make_unique<StubOnlineContext>();
		NoRandomPlacementMultiplayer = true;
		NetworkInitServerConnect(3);
		Hosts[0].SetName("host");
		SetClient(1, client1.host, "one");
		SetClient(2, client2.host, "two");
	}

	~ServerStart()
	{
		NetworkFildes.Close();
		NetConnectRunning = 0;
		NetConnectType = 0;
		NetPlayers = 0;
		NetMesh = false;
		SetNetMesh(savedMeshWanted);
		NoRandomPlacementMultiplayer = savedRandomPlacement;
		OnlineContextHandler = std::move(savedOnline);
		for (int i = 0; i != PlayerMax; ++i) {
			Hosts[i].Clear();
			Map.Info.PlayerType[i] = savedPlayerType[i];
		}
		ServerSetupState.Clear();
		LocalSetupState.Clear();
	}

	/// Run the start with the two clients answering; returns the ms it took
	unsigned long Start(bool &result)
	{
		std::atomic<bool> stop{false};
		std::thread thread1([&] { client1.Run(stop); });
		std::thread thread2([&] { client2.Run(stop); });
		const unsigned long start = GetTicks();
		result = NetworkServerStartGame();
		const unsigned long taken = GetTicks() - start;
		// the go is sent once after the start
		for (int i = 0; i != 50 && !(client1.go && client2.go); ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		stop = true;
		thread1.join();
		thread2.join();
		return taken;
	}

private:
	static void SetClient(int hostIndex, const CHost &host, const char *name)
	{
		Hosts[hostIndex].Host = host.getIp();
		Hosts[hostIndex].Port = host.getPort();
		Hosts[hostIndex].PlyNr = hostIndex;
		Hosts[hostIndex].SetName(name);
	}

public:
	const CHost localHost;
	FakeClient client1;
	FakeClient client2;
private:
	PlayerTypes savedPlayerType[PlayerMax];
	bool savedRandomPlacement;
	bool savedMeshWanted;
	std::unique_ptr<OnlineContext> savedOnline;
};

}

TEST_CASE_FIXTURE(MeshNetwork, "mesh: a client sends its packet to every other device")
{
	REQUIRE(NetworkFildes.IsValid());
	REQUIRE(serverSocket.IsValid());
	REQUIRE(otherSocket.IsValid());

	// the quit of the local player is a command packet like any other
	NetworkQuitGame();

	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		CNetworkPacket packet;
		int commands = 0;
		REQUIRE(Receive(*socket, packet, commands));
		CHECK(commands == 1);
		CHECK(packet.Header.Type[0] == MessageQuit);
		CHECK(packet.Header.OrigPlayer == LocalPlayer);
		CHECK(socket->HasDataToRead(100) == false);
	}

	// the request for a missing packet goes to both as well
	NetworkRecover();
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		CNetworkPacket packet;
		int commands = 0;
		REQUIRE(Receive(*socket, packet, commands));
		CHECK(packet.Header.Type[0] == MessageResend);
		CHECK(socket->HasDataToRead(100) == false);
	}
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh off: a client sends its packet to the server only")
{
	NetMesh = false;
	NetworkQuitGame();

	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(serverSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageQuit);
	CHECK(packet.Header.OrigPlayer == LocalPlayer);
	CHECK(serverSocket.HasDataToRead(100) == false);
	CHECK(otherSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh: the server does not pass on the packets of other players")
{
	BecomeServer();
	CUDPSocket &clientSocket = serverSocket;

	// the packet of player 2 arrives from player 2: stored, sent to nobody
	Parse(SyncPacket(OtherPlayer, 25, 0x1111), otherHost);
	CHECK(StoredSeed(25, OtherPlayer) == 0x1111);
	Parse(SyncPacket(255, 26, 0x2222), otherHost);
	CHECK(StoredSeed(26, OtherPlayer) == 0x2222);
	CHECK(clientSocket.HasDataToRead(200) == false);
	CHECK(otherSocket.HasDataToRead(100) == false);

	// its own packet goes to both clients
	NetworkQuitGame();
	for (CUDPSocket *socket : {&clientSocket, &otherSocket}) {
		CNetworkPacket packet;
		int commands = 0;
		REQUIRE(Receive(*socket, packet, commands));
		CHECK(packet.Header.Type[0] == MessageQuit);
		CHECK(packet.Header.OrigPlayer == ServerPlayer);
		CHECK(socket->HasDataToRead(100) == false);
	}
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh off: the server passes the packet of a client on to the other clients")
{
	NetMesh = false;
	BecomeServer();
	CUDPSocket &clientSocket = serverSocket;

	Parse(SyncPacket(OtherPlayer, 25, 0x1111), otherHost);
	CHECK(StoredSeed(25, OtherPlayer) == 0x1111);

	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(clientSocket, packet, commands));
	CHECK(commands == 1);
	CHECK(packet.Header.Type[0] == MessageSync);
	CHECK(packet.Header.Cycle == 25);
	CHECK(packet.Header.OrigPlayer == OtherPlayer);
	CHECK(clientSocket.HasDataToRead(100) == false);
	// not back to the player it came from
	CHECK(otherSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh: the packet of a player is taken from the address of that player only")
{
	// the server names player 2, as it does when it passes a packet on in the star
	Parse(SyncPacket(OtherPlayer, 25, 0x1111), serverHost);
	CHECK(HasStored(25, OtherPlayer) == false);
	// player 2 names the server, and an address nobody plays at names player 2
	Parse(SyncPacket(ServerPlayer, 25, 0x2222), otherHost);
	Parse(SyncPacket(OtherPlayer, 25, 0x3333), unknownHost);
	Parse(SyncPacket(255, 25, 0x3333), unknownHost);
	CHECK(HasStored(25, ServerPlayer) == false);
	CHECK(HasStored(25, OtherPlayer) == false);
	// a quit of player 2 told by the server is not taken either
	Parse(QuitPacket(OtherPlayer, 26, OtherPlayer), serverHost);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);

	// each player from its own address, with and without its number in the header
	Parse(SyncPacket(OtherPlayer, 25, 0x4444), otherHost);
	CHECK(StoredSeed(25, OtherPlayer) == 0x4444);
	Parse(SyncPacket(255, 26, 0x5555), otherHost);
	CHECK(StoredSeed(26, OtherPlayer) == 0x5555);
	Parse(SyncPacket(ServerPlayer, 25, 0x6666), serverHost);
	CHECK(StoredSeed(25, ServerPlayer) == 0x6666);

	// with the mesh off the server may name player 2 again
	NetMesh = false;
	Parse(SyncPacket(OtherPlayer, 27, 0x7777), serverHost);
	CHECK(StoredSeed(27, OtherPlayer) == 0x7777);
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh: a request for a missing packet is answered to the asker directly")
{
	GameCycle = 5;
	REQUIRE(HasStored(6, LocalPlayer));

	Parse(BuildPacket(255, 6, MessageResend, {}), otherHost);

	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(otherSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageSync);
	CHECK(packet.Header.Cycle == 6);
	CHECK(packet.Header.OrigPlayer == LocalPlayer);
	CHECK(serverSocket.HasDataToRead(200) == false);
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh message: taken from the address of the host slot it names")
{
	const mesh::Msg need = NeedMessage(1234, 0x0005);

	// from host slot 2, through the socket and the event function of the engine
	const std::vector<unsigned char> bytes = MeshBytes(need, 2);
	REQUIRE(bytes.size() == mesh::kWireNeedSize);
	otherSocket.Send(localHost, bytes.data(), bytes.size());
	REQUIRE(EngineReads());
	std::vector<std::pair<int, mesh::Msg>> taken = NetworkTestTakeMeshMessages();
	REQUIRE(taken.size() == 1);
	CHECK(taken[0].first == 2);
	CHECK(taken[0].second.kind == mesh::Kind::Need);
	CHECK(taken[0].second.flags == 1);
	CHECK(taken[0].second.cycle == 1234);
	CHECK(taken[0].second.mask == 0x0005);
	CHECK(Dropped() == 0);
	// a mesh message is no command packet: nothing is stored, nothing is answered
	CHECK(otherSocket.HasDataToRead(100) == false);

	// the server (host slot 0) says it is host slot 2
	serverSocket.Send(localHost, bytes.data(), bytes.size());
	REQUIRE(EngineReads());
	CHECK(NetworkTestTakeMeshMessages().empty());
	CHECK(Dropped() == 1);

	// an address which is no device of the game
	NetworkTestParseMeshEvent(bytes.data(), int(bytes.size()), unknownHost);
	// our own slot named by another device
	const std::vector<unsigned char> own = MeshBytes(need, 1);
	NetworkTestParseMeshEvent(own.data(), int(own.size()), otherHost);
	CHECK(NetworkTestTakeMeshMessages().empty());
	CHECK(Dropped() == 3);

	// every shorter datagram, and a longer one
	for (size_t len = 0; len != bytes.size(); ++len) {
		const std::vector<unsigned char> cut(bytes.begin(), bytes.begin() + len);
		NetworkTestParseMeshEvent(cut.data(), int(cut.size()), otherHost);
	}
	std::vector<unsigned char> longer = bytes;
	longer.push_back(0);
	NetworkTestParseMeshEvent(longer.data(), int(longer.size()), otherHost);
	CHECK(NetworkTestTakeMeshMessages().empty());
	CHECK(Dropped() == 3 + bytes.size() + 1);

	// a truncated one through the socket
	otherSocket.Send(localHost, bytes.data(), bytes.size() - 1);
	REQUIRE(EngineReads());
	CHECK(NetworkTestTakeMeshMessages().empty());
	CHECK(Dropped() == 3 + bytes.size() + 2);

	// the right one again; and not when the game is no mesh
	NetworkTestParseMeshEvent(bytes.data(), int(bytes.size()), otherHost);
	CHECK(NetworkTestTakeMeshMessages().size() == 1);
	NetMesh = false;
	NetworkTestParseMeshEvent(bytes.data(), int(bytes.size()), otherHost);
	CHECK(NetworkTestTakeMeshMessages().empty());
	CHECK(Dropped() == 3 + bytes.size() + 3);
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh message: sent to one device or to every other device, under the own host slot")
{
	const mesh::Msg need = NeedMessage(77, 0x0004);

	CHECK(NetworkSendMesh(2, need) == true);
	std::vector<unsigned char> bytes = ReceiveBytes(otherSocket);
	mesh::Msg got;
	int from = -1;
	REQUIRE(mesh::Deserialize(bytes.data(), bytes.size(), &got, &from));
	CHECK(from == 1); // the host slot of the engine here
	CHECK(got.kind == mesh::Kind::Need);
	CHECK(got.cycle == 77);
	CHECK(got.mask == 0x0004);
	CHECK(serverSocket.HasDataToRead(100) == false);

	// not to ourselves, not to an empty slot, not outside the table
	CHECK(NetworkSendMesh(1, need) == false);
	CHECK(NetworkSendMesh(3, need) == false);
	CHECK(NetworkSendMesh(-1, need) == false);
	CHECK(NetworkSendMesh(PlayerMax, need) == false);
	// not a message the parser of the receiver would refuse
	mesh::Msg bad = need;
	bad.cycle = -1;
	CHECK(NetworkSendMesh(2, bad) == false);
	CHECK(otherSocket.HasDataToRead(100) == false);

	NetworkBroadcastMesh(need);
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		bytes = ReceiveBytes(*socket);
		from = -1;
		REQUIRE(mesh::Deserialize(bytes.data(), bytes.size(), &got, &from));
		CHECK(from == 1);
		CHECK(got.cycle == 77);
		CHECK(socket->HasDataToRead(100) == false);
	}
}

TEST_CASE_FIXTURE(MeshNetwork, "mesh probe: answered to the device in the host slot it names")
{
	NetMesh = false; // the probes travel before the game, when the mesh is not on yet

	// host slot 2 probes: the answer names our slot
	std::vector<unsigned char> probe = ProbeBytes(false, 2);
	REQUIRE(probe.size() == mesh::kWireProbeSize);
	otherSocket.Send(localHost, probe.data(), probe.size());
	REQUIRE(EngineReads());
	std::vector<unsigned char> answer = ReceiveBytes(otherSocket);
	bool ack = false;
	int from = -1;
	REQUIRE(mesh::DeserializeProbe(answer.data(), answer.size(), &ack, &from));
	CHECK(ack == true);
	CHECK(from == 1);
	CHECK(Dropped() == 0);

	// a probe under another slot, or from an address which is no device: no answer
	serverSocket.Send(localHost, probe.data(), probe.size());
	REQUIRE(EngineReads());
	CHECK(serverSocket.HasDataToRead(200) == false);
	NetworkTestParseMeshEvent(probe.data(), int(probe.size()), unknownHost);
	CHECK(Dropped() == 2);

	// neither a probe nor a message of the model
	mesh::Msg got;
	CHECK(mesh::Deserialize(probe.data(), probe.size(), &got, &from) == false);
	probe[2] = 9;
	CHECK(mesh::DeserializeProbe(probe.data(), probe.size(), &ack, &from) == false);
	CHECK(mesh::DeserializeProbe(answer.data(), answer.size() - 1, &ack, &from) == false);
	unsigned char buf[mesh::kWireProbeSize];
	CHECK(mesh::SerializeProbe(false, mesh::kMaxPlayers, buf, sizeof(buf)) == 0);
	CHECK(mesh::SerializeProbe(false, 0, buf, sizeof(buf) - 1) == 0);
}

TEST_CASE_FIXTURE(NetworkInit, "client start: the table after the config, the probes, the mask in the answer, the final state")
{
	const CHost serverHost("127.0.0.1", 6521);
	const CHost localHost("127.0.0.1", 6522);
	const CHost otherHost("127.0.0.1", 6523);
	const CHost thirdHost("127.0.0.1", 6524);
	CUDPSocket serverSocket;
	CUDPSocket otherSocket;
	CUDPSocket thirdSocket; // bound, so that a send to it is no error on this side, but it never answers
	serverSocket.Open(serverHost);
	otherSocket.Open(otherHost);
	thirdSocket.Open(thirdHost);
	NetworkFildes.Open(localHost);
	REQUIRE(serverSocket.IsValid());
	REQUIRE(otherSocket.IsValid());
	REQUIRE(thirdSocket.IsValid());
	REQUIRE(NetworkFildes.IsValid());

	for (const bool meshAtTheEnd : {true, false}) {
		CAPTURE(meshAtTheEnd);
		REQUIRE(NetworkSetupServerAddress("127.0.0.1", 6521) == 0);
		NetworkInitClientConnect();
		NetworkTestSetClientState(ccs_synced);

		// the config as the server sends it: its own entry without address, the clients as it sees them
		CInitMessage_Config config;
		config.clientIndex = 1;
		config.hosts[0].PlyNr = 2;
		config.hosts[0].SetName("server");
		config.hosts[1].Host = localHost.getIp();
		config.hosts[1].Port = localHost.getPort();
		config.hosts[1].PlyNr = 0;
		config.hosts[1].SetName("local");
		config.hosts[2].Host = otherHost.getIp();
		config.hosts[2].Port = otherHost.getPort();
		config.hosts[2].PlyNr = 1;
		config.hosts[2].SetName("other");
		config.hosts[3].Host = thirdHost.getIp();
		config.hosts[3].Port = thirdHost.getPort();
		config.hosts[3].PlyNr = 3;
		config.hosts[3].SetName("third");
		config.mesh = 1;
		const std::vector<unsigned char> configBytes = config.Serialize();
		REQUIRE(NetworkParseSetupEvent(configBytes.data(), int(configBytes.size()), serverHost));

		// what this client holds for each device
		CHECK(GetNetworkState() == ccs_goahead);
		CHECK(NetLocalHostsSlot == 1);
		CHECK(NetLocalPlayerNumber == 0);
		CHECK(NetPlayers == 4);
		CHECK(CHost(Hosts[0].Host, Hosts[0].Port) == serverHost); // the address it dialled
		CHECK(Hosts[0].PlyNr == 2);
		CHECK(CHost(Hosts[2].Host, Hosts[2].Port) == otherHost);  // as the server sees it
		CHECK(Hosts[2].PlyNr == 1);
		CHECK(CHost(Hosts[3].Host, Hosts[3].Port) == thirdHost);
		CHECK(Hosts[3].PlyNr == 3);
		CHECK(Hosts[1].Port == CNetworkParameter::Instance.localPort); // itself: a place holder, never sent to
		CHECK(CHost(Hosts[1].Host, Hosts[1].Port) != localHost);
		CHECK(FindHostIndexBy(serverHost) == 0);
		CHECK(FindHostIndexBy(otherHost) == 2);
		CHECK(FindHostIndexBy(thirdHost) == 3);
		CHECK(Hosts[4].IsValid() == false);
		CHECK(NetMesh == false);
		CHECK(NetworkTestMeshProbeMask() == 0);

		// it probes the two other clients, not the server, and answers the config without a peer
		NetworkProcessClientRequest();
		std::vector<unsigned char> bytes = MeshNetwork::ReceiveBytes(otherSocket);
		bool ack = true;
		int from = -1;
		REQUIRE(mesh::DeserializeProbe(bytes.data(), bytes.size(), &ack, &from));
		CHECK(ack == false);
		CHECK(from == 1);
		// (the answer is rate limited: it leaves up to 250 ms after the config)
		for (int i = 0; i != 100 && !serverSocket.HasDataToRead(10); ++i) {
			NetworkProcessClientRequest();
		}
		bytes = MeshNetwork::ReceiveBytes(serverSocket);
		REQUIRE(bytes.size() == CInitMessage_ConfigAck::Size());
		CInitMessage_ConfigAck configAck;
		configAck.Deserialize(bytes.data());
		CHECK(configAck.GetHeader().GetType() == MessageInit_FromClient);
		CHECK(configAck.GetHeader().GetSubType() == ICMConfig);
		CHECK(configAck.peers == 0);

		// host slot 2 answers; host slot 3 is probed too and never does
		CHECK(MeshNetwork::ReceiveBytes(thirdSocket).size() == mesh::kWireProbeSize);
		const std::vector<unsigned char> probeAck = MeshNetwork::ProbeBytes(true, 2);
		otherSocket.Send(localHost, probeAck.data(), probeAck.size());
		REQUIRE(MeshNetwork::EngineReads());
		CHECK(NetworkTestMeshProbeMask() == 0x0004);

		// the next answers carry the peer; the one which answered is not probed again
		const unsigned long start = GetTicks();
		uint16_t reported = 0;
		while (reported == 0 && GetTicks() - start < 2000) {
			NetworkProcessClientRequest();
			bytes = MeshNetwork::ReceiveBytes(serverSocket, 20);
			if (bytes.size() == CInitMessage_ConfigAck::Size()) {
				configAck.Deserialize(bytes.data());
				reported = configAck.peers;
			}
		}
		CHECK(reported == 0x0004);
		while (otherSocket.HasDataToRead(0)) {
			MeshNetwork::ReceiveBytes(otherSocket); // probes sent before the answer was read
		}
		while (thirdSocket.HasDataToRead(0)) {
			MeshNetwork::ReceiveBytes(thirdSocket);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		NetworkProcessClientRequest();
		CHECK(otherSocket.HasDataToRead(100) == false);
		CHECK(MeshNetwork::ReceiveBytes(thirdSocket).size() == mesh::kWireProbeSize);

		// the final state of the server says how the game runs
		CInitMessage_State state(MessageInit_FromServer, ServerSetupState);
		state.mesh = meshAtTheEnd ? 1 : 0;
		const std::vector<unsigned char> stateBytes = state.Serialize();
		REQUIRE(NetworkParseSetupEvent(stateBytes.data(), int(stateBytes.size()), serverHost));
		CHECK(GetNetworkState() == ccs_started);
		CHECK(NetMesh == meshAtTheEnd);

		while (serverSocket.HasDataToRead(0)) {
			MeshNetwork::ReceiveBytes(serverSocket);
		}
		while (thirdSocket.HasDataToRead(0)) {
			MeshNetwork::ReceiveBytes(thirdSocket);
		}
	}

	// a config which does not ask for the mesh: the plain answer, no probe, and no mesh whatever the state says
	NetworkInitClientConnect();
	NetworkTestSetClientState(ccs_synced);
	CInitMessage_Config config;
	config.clientIndex = 1;
	config.hosts[0].SetName("server");
	config.hosts[1].PlyNr = 1;
	config.hosts[1].SetName("local");
	config.hosts[2].Host = otherHost.getIp();
	config.hosts[2].Port = otherHost.getPort();
	config.hosts[2].PlyNr = 2;
	config.hosts[2].SetName("other");
	const std::vector<unsigned char> configBytes = config.Serialize();
	REQUIRE(NetworkParseSetupEvent(configBytes.data(), int(configBytes.size()), serverHost));
	CHECK(CHost(Hosts[0].Host, Hosts[0].Port) == serverHost);
	CHECK(CHost(Hosts[2].Host, Hosts[2].Port) == otherHost);
	for (int i = 0; i != 100 && !serverSocket.HasDataToRead(10); ++i) {
		NetworkProcessClientRequest();
	}
	const std::vector<unsigned char> bytes = MeshNetwork::ReceiveBytes(serverSocket);
	CHECK(bytes.size() == CInitMessage_Header::Size());
	CHECK(otherSocket.HasDataToRead(200) == false);
	CHECK(thirdSocket.HasDataToRead(0) == false);
	CInitMessage_State state(MessageInit_FromServer, ServerSetupState);
	state.mesh = 1;
	const std::vector<unsigned char> stateBytes = state.Serialize();
	REQUIRE(NetworkParseSetupEvent(stateBytes.data(), int(stateBytes.size()), serverHost));
	CHECK(GetNetworkState() == ccs_started);
	CHECK(NetMesh == false);

	NetworkFildes.Close();
	serverSocket.Close();
	otherSocket.Close();
	thirdSocket.Close();
	NetConnectRunning = 0;
	NetConnectType = 0;
	NetPlayers = 0;
	NetLocalHostsSlot = 0;
	NetMesh = false;
	for (int i = 0; i != PlayerMax; ++i) {
		Hosts[i].Clear();
	}
	ServerSetupState.Clear();
	LocalSetupState.Clear();
}

TEST_CASE("server start: every client reached every other client, the game is a mesh")
{
	ServerStart start(6531, true, 0x0004, 0x0002);
	REQUIRE(NetworkFildes.IsValid());
	REQUIRE(start.client1.socket.IsValid());
	REQUIRE(start.client2.socket.IsValid());
	SetNetMesh(true);

	// what the server holds: itself without address, each client at the address it joined from
	CHECK(NetLocalHostsSlot == 0);
	CHECK(Hosts[0].Host == 0);
	CHECK(Hosts[0].Port == 0);

	bool result = false;
	const unsigned long taken = start.Start(result);
	CHECK(result == true);
	CHECK(taken < 2000); // the answers with the peers end the wait, not the bound
	CHECK(NetMesh == true);
	CHECK(NetworkTestMeshProbeFailed().empty());
	CHECK(CHost(Hosts[1].Host, Hosts[1].Port) == start.client1.host);
	CHECK(CHost(Hosts[2].Host, Hosts[2].Port) == start.client2.host);

	for (const FakeClient *client : {&start.client1, &start.client2}) {
		CHECK(client->configs >= 1);
		CHECK(client->states >= 1);
		CHECK(client->stateMesh == 1);
		CHECK(client->go == true);
		// the table of the config: the server without address, both clients as the server sees them
		CHECK(client->config.mesh == 1);
		CHECK(client->config.hosts[0].Host == 0);
		CHECK(client->config.hosts[0].Port == 0);
		CHECK(CHost(client->config.hosts[1].Host, client->config.hosts[1].Port) == start.client1.host);
		CHECK(CHost(client->config.hosts[2].Host, client->config.hosts[2].Port) == start.client2.host);
		CHECK(client->config.hosts[3].IsValid() == false);
	}
	CHECK(start.client1.config.clientIndex == 1);
	CHECK(start.client2.config.clientIndex == 2);
}

TEST_CASE("server start: a pair of clients which do not reach each other, the game runs over the server")
{
	// client 1 never hears client 2
	ServerStart start(6534, true, 0x0000, 0x0002);
	REQUIRE(NetworkFildes.IsValid());
	SetNetMesh(true);

	bool result = false;
	const unsigned long taken = start.Start(result);
	CHECK(result == true);
	CHECK(taken >= 2000); // the bound of the probe
	CHECK(taken < 4000);
	CHECK(NetMesh == false);
	const std::vector<std::pair<int, int>> failed = NetworkTestMeshProbeFailed();
	REQUIRE(failed.size() == 1);
	CHECK(failed[0].first == 1);
	CHECK(failed[0].second == 2);

	for (const FakeClient *client : {&start.client1, &start.client2}) {
		CHECK(client->config.mesh == 1);
		CHECK(client->states >= 1);
		CHECK(client->stateMesh == 0);
		CHECK(client->go == true);
	}
}

TEST_CASE("server start: the mesh not asked for, the start is the one of the star")
{
	// the clients answer with the plain header, as they do to a config without the mesh byte
	ServerStart start(6537, false, 0, 0);
	REQUIRE(NetworkFildes.IsValid());
	SetNetMesh(false);

	bool result = false;
	const unsigned long taken = start.Start(result);
	CHECK(result == true);
	CHECK(taken < 2000);
	CHECK(NetMesh == false);
	for (const FakeClient *client : {&start.client1, &start.client2}) {
		CHECK(client->config.mesh == 0);
		CHECK(client->stateMesh == 0);
		CHECK(client->go == true);
	}
}

TEST_CASE("server start: clients which answer without peers get no mesh")
{
	ServerStart start(6540, false, 0, 0);
	REQUIRE(NetworkFildes.IsValid());
	SetNetMesh(true);

	bool result = false;
	const unsigned long taken = start.Start(result);
	CHECK(result == true);
	CHECK(taken >= 2000);
	CHECK(NetMesh == false);
	CHECK(NetworkTestMeshProbeFailed().size() == 2);
	CHECK(start.client1.stateMesh == 0);
	CHECK(start.client2.stateMesh == 0);
}

TEST_CASE("spell id: the type byte of a command packet has no room for the mesh byte")
{
	CHECK(MaxNetworkSpellId == 93);
	CHECK(MessageCommandSpellCast + MaxNetworkSpellId == MessageMesh - 1);
	CHECK(IsNetworkSpellId(0) == true);
	CHECK(IsNetworkSpellId(MaxNetworkSpellId) == true);
	CHECK(IsNetworkSpellId(MaxNetworkSpellId + 1) == false); // would be read as MessageMesh
	CHECK(IsNetworkSpellId(-1) == false);
	// every id let through gives a type below MessageMesh, with and without the flush bit taken off
	for (int id = 0; id <= MaxNetworkSpellId; ++id) {
		CHECK(((MessageCommandSpellCast + id) & 0x7F) < MessageMesh);
	}
}

//
// Mesh game with the model (net_mesh_glue.h): relay on request, the removal round, the return
// window. One real engine endpoint again; the two sockets speak the wire format and send command
// packets as the devices of player 0 (the lowest, the one which decides) and of player 2 would.
// Time is real (GetTicks) with bounds of a few hundred ms.
//

// Not run here: the cycle of an installed quit itself. NetworkExecCommand_Quit ends in CommandQuit,
// which puts a text on the screen, and the test world has no font. The tests stop before that
// cycle and show that the quit is in the queue and that the cycle may run.

namespace {

class MeshGame : public MeshNetwork
{
public:
	/// What a socket of a scripted device received
	struct Seen
	{
		std::vector<mesh::Msg> meshes;                          /// mesh messages of the engine
		std::vector<CNetworkPacket> packets;                    /// command packets
		std::vector<std::pair<int, CNetworkPacket>> relays;     /// (host slot of the sender, packet)
	};

	/// With `release` the two other devices have loaded and the game runs; without it the game is held
	explicit MeshGame(bool release = true)
	{
		// the model names nobody as awaited in the first second of the clock (net_mesh.cpp, lastWait_)
		while (GetTicks() < 1100) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		// command packets come through NetworkEvent here, which asks the online service first
		savedOnline = std::move(OnlineContextHandler);
		OnlineContextHandler = std::make_unique<StubOnlineContext>();
		GameCycle = 0;
		NetworkOnStartGame(); // with the default bounds
		heldFromStart = NetworkIsHeld() && !NetworkInSync;
		started = NetworkTestMeshStart(Bounds());
		NetworkInSync = false; // held, as NetworkOnStartGame leaves it
		if (started && release) {
			NetworkGameLoaded();
			Send(serverSocket, MeshBytes(LoadedBy(0x7), 0));
			Send(otherSocket, MeshBytes(LoadedBy(0x7), 2));
			EngineReadsAll(200);
			NetworkTestMeshUpdate();
			Take(serverSocket);
			Take(otherSocket);
			NetworkTestTakeMeshMessages();
		}
	}

	~MeshGame()
	{
		NetworkTestMeshStop();
		OnlineContextHandler = std::move(savedOnline);
	}

	static mesh::Params Bounds()
	{
		mesh::Params params;

		params.dropMs = 800;
		params.allowanceMs = 2000;
		params.exhaustedDropMs = 200;
		params.plateMs = 150;
		params.hearMs = 300;
		params.statusMs = 50;
		params.needMs = 30;
		params.relayAfterMs = 90;
		params.loadMs = 1500;
		params.loadPingDropMs = 500;
		params.loadedMs = 50;
		params.lingerQuietMs = 300;
		params.lingerCapMs = 1000;
		return params;
	}

	/// The report of a device which has loaded and knows the devices in `seen` as loaded
	static mesh::Msg LoadedBy(uint16_t seen)
	{
		mesh::Msg loaded;
		loaded.kind = mesh::Kind::Loaded;
		loaded.mask = seen;
		return loaded;
	}

	/// The packet a device sends for a cycle in which its player does nothing
	static std::vector<unsigned char> Idle(int player, unsigned long cycle)
	{
		return BuildPacket(player, cycle, MessageSync, IdleData());
	}

	/// Its one command: the sync values, which are the same on every device
	static std::vector<unsigned char> IdleData()
	{
		CNetworkCommandSync nc;
		nc.syncSeed = SyncRandSeed;
		nc.syncHash = SyncHash;
		std::vector<unsigned char> data(nc.Size());
		nc.Serialize(data.data());
		return data;
	}

	void Send(CUDPSocket &socket, const std::vector<unsigned char> &bytes) const
	{
		socket.Send(localHost, bytes.data(), bytes.size());
	}

	/// Both devices deliver their packets up to and including the cycles given
	void Deliver(unsigned long serverLast, unsigned long otherLast)
	{
		for (unsigned long cycle = CNetworkParameter::Instance.NetworkLag + 1; cycle <= serverLast; ++cycle) {
			Send(serverSocket, Idle(ServerPlayer, cycle));
		}
		for (unsigned long cycle = CNetworkParameter::Instance.NetworkLag + 1; cycle <= otherLast; ++cycle) {
			Send(otherSocket, Idle(OtherPlayer, cycle));
		}
		EngineReadsAll(200);
	}

	/// The engine reads what waits on its socket
	static void EngineReadsAll(int wait = 20)
	{
		while (NetworkFildes.HasDataToRead(wait)) {
			NetworkEvent();
			wait = 20;
		}
	}

	/// The cycles of the game loop while the network lets them run
	static int RunCycles(int max = 1000)
	{
		int n = 0;

		for (; n != max && NetworkInSync; ++n) {
			++GameCycle;
			NetworkCommands();
		}
		return n;
	}

	/// Everything which waits on the socket of a scripted device
	static Seen Take(CUDPSocket &socket)
	{
		Seen seen;

		while (socket.HasDataToRead(0)) {
			unsigned char buf[2048];
			CHost from;
			const int len = socket.Recv(buf, sizeof(buf), &from);
			if (len <= 0) {
				continue;
			}
			mesh::Msg message;
			int slot = -1;
			const unsigned char *relayed = nullptr;
			size_t relayedSize = 0;
			if (mesh::Deserialize(buf, len, &message, &slot)) {
				seen.meshes.push_back(message);
			} else if (mesh::DeserializeRelay(buf, len, &slot, &relayed, &relayedSize)) {
				CNetworkPacket packet;
				int commands = 0;
				packet.Deserialize(relayed, relayedSize, &commands);
				seen.relays.emplace_back(slot, packet);
			} else if (buf[0] != MessageMesh && len >= int(CNetworkPacketHeader::Size())) {
				CNetworkPacket packet;
				int commands = 0;
				packet.Deserialize(buf, len, &commands);
				seen.packets.push_back(packet);
			}
		}
		return seen;
	}

	static unsigned char StoredType(unsigned long cycle, int player)
	{
		unsigned char type = MessageNone;
		std::vector<unsigned char> data;

		return NetworkTestGetCommand(cycle, player, 0, type, data) ? type : (unsigned char) MessageNone;
	}

	/// The quit of that player is in his place of that cycle
	static bool QuitStored(unsigned long cycle, int player)
	{
		unsigned char type = MessageNone;
		std::vector<unsigned char> data;

		if (!NetworkTestGetCommand(cycle, player, 0, type, data) || type != MessageQuit) {
			return false;
		}
		CNetworkCommandQuit nc;
		nc.Deserialize(data.data());
		return nc.player == player;
	}

	/// The reason in the quit of that player in his place of that cycle, -1 if there is none
	static int QuitReasonStored(unsigned long cycle, int player)
	{
		unsigned char type = MessageNone;
		std::vector<unsigned char> data;

		if (!NetworkTestGetCommand(cycle, player, 0, type, data) || type != MessageQuit) {
			return -1;
		}
		CNetworkCommandQuit nc;
		nc.Deserialize(data.data());
		return nc.reason;
	}

	/// The reason in a quit packet, -1 if it is none
	static int QuitReasonOf(const CNetworkPacket &packet)
	{
		if (packet.Header.Type[0] != MessageQuit || packet.Command[0].size() != CNetworkCommandQuit::Size()) {
			return -1;
		}
		CNetworkCommandQuit nc;
		nc.Deserialize(packet.Command[0].data());
		return nc.reason;
	}

	static mesh::Msg Commit(int hostSlot, unsigned long cycle, uint16_t alive)
	{
		mesh::Msg commit;
		commit.kind = mesh::Kind::Commit;
		commit.player = uint8_t(hostSlot);
		commit.cycle = int32_t(cycle);
		commit.mask = alive;
		commit.flags = uint8_t(mesh::Reason::Lost);
		return commit;
	}

	static std::vector<unsigned char> RelayBytes(const std::vector<unsigned char> &packet, int from)
	{
		unsigned char buf[mesh::kWireRelayMaxSize];
		const size_t size = mesh::SerializeRelay(packet.data(), packet.size(), from, buf, sizeof(buf));
		return std::vector<unsigned char>(buf, buf + size);
	}

	bool started = false;
	bool heldFromStart = false; /// as NetworkOnStartGame left the game
private:
	std::unique_ptr<OnlineContext> savedOnline;
};

/// The game as it is after its load: held
class MeshGameHeld : public MeshGame
{
public:
	MeshGameHeld() : MeshGame(false) {}

	/// The engine reads, the model ticks; what the game loop does in a frame while it waits
	static void Frame(int wait = 10)
	{
		EngineReadsAll(wait);
		NetworkTestMeshUpdate();
	}

	static int Count(const Seen &seen, mesh::Kind kind)
	{
		int n = 0;
		for (const mesh::Msg &message : seen.meshes) {
			n += message.kind == kind ? 1 : 0;
		}
		return n;
	}
};

}

TEST_CASE_FIXTURE(InGameNetworkWithSockets, "mesh off: there is no model")
{
	CHECK(NetworkTestMeshStart(mesh::Params()) == false);
	CHECK(NetworkTestMeshGlue() == nullptr);
	CHECK(NetworkGetWaitState().Player == -1);
	CHECK(NetworkGetWaitState().SelfOut == false);
	NetworkTestMeshUpdate(); // nothing
	// no start barrier: the game runs from its first frame, nothing is said while it loads
	GameCycle = 0;
	NetworkOnStartGame();
	CHECK(NetworkInSync);
	CHECK(NetworkIsHeld() == false);
	CHECK(NetworkGetWaitState().Held == false);
	std::this_thread::sleep_for(std::chrono::milliseconds(260));
	NetworkLoadingPing();
	NetworkGameLoaded();
	CHECK(serverSocket.HasDataToRead(100) == false);
	CHECK(otherSocket.HasDataToRead(0) == false);
	CHECK(NetworkGetQuitReason(OtherPlayer) == -1);
	GameCycle = 20;
	// the request for a missing packet is the one of the star
	NetworkRecover();
	CNetworkPacket packet;
	int commands = 0;
	REQUIRE(Receive(serverSocket, packet, commands));
	CHECK(packet.Header.Type[0] == MessageResend);
	CHECK(otherSocket.HasDataToRead(100) == false);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: cycles run while every device delivers, and stop at the first missing packet")
{
	REQUIRE(started);
	REQUIRE(NetworkTestMeshGlue() != nullptr);
	CHECK(NetworkTestMeshGlue()->PlayerOfSlot(0) == ServerPlayer);
	CHECK(NetworkTestMeshGlue()->PlayerOfSlot(1) == LocalPlayer);
	CHECK(NetworkTestMeshGlue()->PlayerOfSlot(2) == OtherPlayer);

	// the cycles 1..lag need nothing from the others
	CHECK(RunCycles() == 10);
	CHECK(GameCycle == 10);
	CHECK(NetworkInSync == false);
	// what the game loop calls while it waits: nobody was heard yet, nobody is timed out
	NetworkRecover();
	CHECK(NetworkInSync == false);
	CHECK(NetworkGetWaitState().Player == -1);
	Deliver(20, 15);
	NetworkRecover();
	CHECK(NetworkInSync);
	CHECK(RunCycles() == 5);
	CHECK(GameCycle == 15);
	CHECK(NetworkInSync == false);

	// the own packets went to both, one per cycle, none twice
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		const Seen seen = Take(*socket);
		REQUIRE(seen.packets.size() == 15);
		for (size_t i = 0; i != seen.packets.size(); ++i) {
			CHECK(seen.packets[i].Header.OrigPlayer == LocalPlayer);
			CHECK(seen.packets[i].Header.Type[0] == MessageSync);
			CHECK(seen.packets[i].Header.Cycle == 11 + i);
		}
		CHECK(seen.relays.empty());
		// the wait asked for the missing packets with the model's request, not with the star's
		bool asked = false;
		for (const mesh::Msg &message : seen.meshes) {
			asked = asked || (message.kind == mesh::Kind::Need && message.cycle == 11 && message.mask == 0x5);
		}
		CHECK(asked);
	}
	CHECK(NetworkGetWaitState().Player == -1);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a silent player is waited for and removed at the cycle the devices agree on")
{
	REQUIRE(started);
	Deliver(20, 15);
	RunCycles();
	REQUIRE(GameCycle == 15);
	Take(serverSocket);
	Take(otherSocket);

	// player 2 falls silent; player 0 is there and holds nothing more of player 2 either
	bool waited = false;
	bool removing = false;
	bool asked = false;
	bool askedEverybody = false;
	bool haveStatus = false;
	int firstLeft = 0;
	int lastLeft = 0;
	int allowance = 0;
	mesh::Msg status;
	std::vector<CNetworkPacket> packets;
	const unsigned long start = GetTicks();
	while (GetTicks() - start < 3000 && !haveStatus) {
		Send(serverSocket, Idle(ServerPlayer, 16)); // again and again: it changes nothing, but he is heard
		EngineReadsAll(10);
		NetworkTestMeshUpdate();
		const CNetworkWaitState state = NetworkGetWaitState();
		if (state.Player == OtherPlayer && !state.Removing) {
			if (!waited) {
				firstLeft = state.MsLeft;
				allowance = state.AllowanceMsLeft;
			}
			lastLeft = state.MsLeft;
			waited = true;
		}
		removing = removing || (state.Player == OtherPlayer && state.Removing);
		CHECK(state.SelfOut == false);
		const Seen seen = Take(serverSocket);
		packets.insert(packets.end(), seen.packets.begin(), seen.packets.end());
		for (const mesh::Msg &message : seen.meshes) {
			if (message.kind == mesh::Kind::Need) {
				CHECK(message.cycle == 16);
				CHECK(message.mask == (1u << 2));
				asked = true;
				askedEverybody = askedEverybody || message.flags == 1;
			} else if (message.kind == mesh::Kind::Status && !(message.mask & (1u << 2))) {
				status = message;
				haveStatus = true;
			}
		}
	}
	const unsigned long taken = GetTicks() - start;
	REQUIRE(haveStatus);
	CHECK(taken >= 600); // not before the bound of 800 ms, counted from the last packet of player 2
	CHECK(taken < 2000);
	CHECK(waited);
	CHECK(firstLeft <= 800 - 150);
	CHECK(firstLeft > 300);
	CHECK(lastLeft < firstLeft); // it counts down
	CHECK(allowance > 1000);
	CHECK(allowance <= 2000 - 150);
	CHECK(removing);
	CHECK(asked);
	CHECK(askedEverybody);
	CHECK(status.mask == 0x3);
	CHECK(status.have[2] == 15);
	// nothing ran, and nothing was decided by the own clock
	CHECK(GameCycle == 15);
	CHECK(NetworkInSync == false);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);
	CHECK(QuitStored(16, OtherPlayer) == false);
	// no request of the star, no quit made up for the silent player
	const Seen other = Take(otherSocket);
	packets.insert(packets.end(), other.packets.begin(), other.packets.end());
	for (const CNetworkPacket &packet : packets) {
		CHECK(packet.Header.Type[0] != MessageResend);
		CHECK(packet.Header.Type[0] != MessageQuit);
	}

	// frozen: what comes from his address now is not taken, or the devices would hold different packets
	Send(otherSocket, Idle(OtherPlayer, 16));
	EngineReadsAll(200);
	CHECK(HasStored(16, OtherPlayer) == false);
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync == false);

	// the lowest device names the cycle
	Send(serverSocket, MeshBytes(Commit(2, 16, 0x3), 0));
	EngineReadsAll(200);
	CHECK(NetworkTestPlayerQuit(OtherPlayer));
	CHECK(QuitStored(16, OtherPlayer));
	CHECK(QuitReasonStored(16, OtherPlayer) == NetQuitLost);
	CHECK(NetworkGetQuitReason(OtherPlayer) == -1); // known to the game when the quit has run
	CHECK(NetworkGetWaitState().RemovedAtStart == 0);
	CHECK(QuitStored(15, OtherPlayer) == false);
	CHECK(QuitStored(17, OtherPlayer) == false);
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync);
	CHECK(NetworkGetWaitState().Player == -1);

	CHECK(GameCycle == 15); // cycle 16, with the quit in it, is the next one
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a player who returns before the bound plays on from the same cycle")
{
	REQUIRE(started);
	Deliver(25, 15);
	RunCycles();
	REQUIRE(GameCycle == 15);
	Take(serverSocket);

	bool waited = false;
	const unsigned long start = GetTicks();
	while (GetTicks() - start < 450) {
		Send(serverSocket, Idle(ServerPlayer, 16));
		EngineReadsAll(10);
		NetworkTestMeshUpdate();
		const CNetworkWaitState state = NetworkGetWaitState();
		waited = waited || (state.Player == OtherPlayer && state.MsLeft > 0 && !state.Removing);
		CHECK(state.Removing == false);
	}
	CHECK(waited);
	CHECK(GameCycle == 15);
	CHECK(NetworkInSync == false);

	// he is back: he answers the request with the packets he made before he fell silent
	for (unsigned long cycle = 16; cycle <= 25; ++cycle) {
		Send(otherSocket, Idle(OtherPlayer, cycle));
	}
	EngineReadsAll(200);
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync);
	CHECK(NetworkGetWaitState().Player == -1);
	CHECK(RunCycles(1) == 1);
	CHECK(GameCycle == 16); // the cycle the game stopped before
	RunCycles();
	CHECK(GameCycle == 25);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);
	CHECK(Hosts[2].IsValid());
	for (unsigned long cycle = 16; cycle <= 25; ++cycle) {
		CHECK(StoredType(cycle, OtherPlayer) == MessageSync);
	}
	// no removal round was opened
	for (const mesh::Msg &message : Take(serverSocket).meshes) {
		CHECK(message.kind == mesh::Kind::Need);
	}
	// 450 ms of his 2000 are used
	REQUIRE(NetworkTestMeshGlue() != nullptr);
	const int64_t rest = NetworkTestMeshGlue()->Model().AllowanceLeft(2, GetTicks());
	CHECK(rest <= 2000 - 400);
	CHECK(rest >= 2000 - 800);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: the lowest device is the silent one, the next one decides")
{
	REQUIRE(started);
	Deliver(15, 20);
	RunCycles();
	REQUIRE(GameCycle == 15);
	Take(serverSocket);
	Take(otherSocket);

	// player 0 falls silent. We are the lowest who is left; player 2 sees what we see.
	bool committed = false;
	mesh::Msg commit;
	uint32_t sequence = 0;
	const unsigned long start = GetTicks();
	while (GetTicks() - start < 3000 && !committed) {
		Send(otherSocket, Idle(OtherPlayer, 16));
		EngineReadsAll(10);
		NetworkTestMeshUpdate();
		for (const mesh::Msg &message : Take(otherSocket).meshes) {
			if (message.kind == mesh::Kind::Status && !(message.mask & 1u)) {
				mesh::Msg view = message;
				view.player = 2;
				view.data = ++sequence;
				view.want = 0;
				Send(otherSocket, MeshBytes(view, 2));
			} else if (message.kind == mesh::Kind::Commit) {
				commit = message;
				committed = true;
			}
		}
	}
	REQUIRE(committed);
	CHECK(GetTicks() - start >= 600);
	CHECK(commit.player == 0);
	CHECK(commit.cycle == 16); // one after the last packet of player 0 which both hold
	CHECK(commit.mask == 0x6);
	CHECK(commit.flags == uint8_t(mesh::Reason::Lost));
	CHECK(QuitStored(16, ServerPlayer));
	CHECK(NetworkTestPlayerQuit(ServerPlayer));
	// the removed device is told as well (it may only be cut off, B.5 rule 10)
	bool told = false;
	for (const mesh::Msg &message : Take(serverSocket).meshes) {
		told = told || (message.kind == mesh::Kind::Commit && message.player == 0 && message.cycle == 16);
	}
	CHECK(told);
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync);
	CHECK(NetworkGetWaitState().Player == -1);
	CHECK(GameCycle == 15);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a commit is applied at exactly its cycle, a stranger's commit is not")
{
	REQUIRE(started);
	Deliver(20, 20);
	CHECK(RunCycles(12) == 12);
	const std::vector<unsigned char> commit = MeshBytes(Commit(2, 18, 0x3), 0);

	// from an address which is no device of the game
	CUDPSocket strangerSocket;
	strangerSocket.Open(unknownHost);
	REQUIRE(strangerSocket.IsValid());
	strangerSocket.Send(localHost, commit.data(), commit.size());
	EngineReadsAll(200);
	// from a device of the game under the host slot of another one
	Send(otherSocket, commit);
	EngineReadsAll(200);
	CHECK(Dropped() == 2);
	// decided by a group this device is not part of
	Send(serverSocket, MeshBytes(Commit(2, 18, 0x1), 0));
	EngineReadsAll(200);
	strangerSocket.Close();
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);
	CHECK(StoredType(18, OtherPlayer) == MessageSync);
	CHECK(NetworkTestTakeMeshMessages().size() == 1);

	// from a device of the game, for a cycle this device already holds a packet of: the quit takes its place
	Send(serverSocket, commit);
	EngineReadsAll(200);
	CHECK(NetworkTestPlayerQuit(OtherPlayer));
	CHECK(QuitStored(18, OtherPlayer));
	CHECK(StoredType(17, OtherPlayer) == MessageSync);
	// a second commit with another cycle changes nothing
	Send(serverSocket, MeshBytes(Commit(2, 19, 0x3), 0));
	EngineReadsAll(200);
	CHECK(QuitStored(18, OtherPlayer));
	CHECK(QuitStored(19, OtherPlayer) == false);

	// the cycles before it run with his packets, and then cycle 18 may run
	CHECK(RunCycles(5) == 5);
	CHECK(GameCycle == 17);
	CHECK(Hosts[2].IsValid());
	CHECK(NetworkInSync);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a device the others removed stops, whatever its queue holds")
{
	REQUIRE(started);
	Deliver(20, 20);
	CHECK(RunCycles(12) == 12);
	CHECK(NetworkInSync);

	Send(serverSocket, MeshBytes(Commit(1, 16, 0x5), 0)); // about us
	EngineReadsAll(200);
	const CNetworkWaitState state = NetworkGetWaitState();
	CHECK(state.SelfOut);
	CHECK(state.SelfOutReason == int(mesh::Reason::ConnectionLost));
	CHECK(state.Player == -1);
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync == false);
	CHECK(RunCycles() == 0);
	CHECK(GameCycle == 12);
	// nothing was written into the queue for it
	CHECK(StoredType(16, LocalPlayer) == MessageSync);
	CHECK(NetworkTestPlayerQuit(LocalPlayer) == false);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a request is answered with the packets held, to the asker")
{
	REQUIRE(started);
	Deliver(15, 15);
	CHECK(RunCycles(2) == 2);
	Take(serverSocket);
	Take(otherSocket);

	mesh::Msg need;
	need.kind = mesh::Kind::Need;
	need.cycle = 12;
	need.mask = uint16_t(1u << 0); // the packets of player 0
	// without the word "anybody" only the player himself answers
	Send(otherSocket, MeshBytes(need, 2));
	EngineReadsAll(200);
	CHECK(Take(otherSocket).relays.empty());

	need.flags = 1;
	Send(otherSocket, MeshBytes(need, 2));
	EngineReadsAll(200);
	Seen seen = Take(otherSocket);
	REQUIRE(seen.relays.size() == 4); // 12, 13, 14, 15
	for (size_t i = 0; i != seen.relays.size(); ++i) {
		const CNetworkPacket &packet = seen.relays[i].second;
		CHECK(seen.relays[i].first == 1); // sent under our host slot
		CHECK(packet.Header.OrigPlayer == ServerPlayer);
		CHECK(packet.Header.Cycle == 12 + i);
		CHECK(packet.Header.Type[0] == MessageSync);
		CHECK(packet.Header.Type[1] == MessageNone);
		CHECK(packet.Command[0] == IdleData());
	}
	CHECK(seen.packets.empty());

	// our own packets go as the packets they are
	need.flags = 0;
	need.mask = uint16_t(1u << 1);
	Send(otherSocket, MeshBytes(need, 2));
	EngineReadsAll(200);
	seen = Take(otherSocket);
	CHECK(seen.relays.empty());
	REQUIRE(seen.packets.size() == 1); // made so far: up to cycle 2 + lag
	CHECK(seen.packets[0].Header.OrigPlayer == LocalPlayer);
	CHECK(seen.packets[0].Header.Cycle == 12);
	// nothing of all that went to the device which did not ask
	seen = Take(serverSocket);
	CHECK(seen.relays.empty());
	CHECK(seen.packets.empty());
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a packet which does not arrive is asked for and taken from a device which holds it")
{
	REQUIRE(started);
	Deliver(25, 15);
	RunCycles();
	REQUIRE(GameCycle == 15);
	Take(serverSocket);
	Take(otherSocket);

	// first player 2 himself is asked, then everybody
	bool askedHim = false;
	bool askedEverybody = false;
	int requests = 0;
	const unsigned long start = GetTicks();
	while (GetTicks() - start < 1000 && !askedEverybody) {
		EngineReadsAll(5);
		NetworkTestMeshUpdate();
		for (const mesh::Msg &message : Take(otherSocket).meshes) {
			askedHim = askedHim || (message.kind == mesh::Kind::Need && message.cycle == 16 && message.mask == 0x4);
		}
		for (const mesh::Msg &message : Take(serverSocket).meshes) {
			if (message.kind == mesh::Kind::Need) {
				++requests;
				CHECK(message.cycle == 16);
				CHECK(message.mask == 0x4);
				askedEverybody = askedEverybody || message.flags == 1;
			}
		}
	}
	const unsigned long taken = GetTicks() - start;
	CHECK(askedHim);
	REQUIRE(askedEverybody);
	CHECK(taken >= 60);
	CHECK(requests <= int(taken / 30) + 2); // one request per 30 ms

	// what is no relay for this device
	CUDPSocket strangerSocket;
	strangerSocket.Open(unknownHost);
	REQUIRE(strangerSocket.IsValid());
	const std::vector<unsigned char> relay = RelayBytes(Idle(OtherPlayer, 16), 0);
	strangerSocket.Send(localHost, relay.data(), relay.size());    // not a device of the game
	EngineReadsAll(200);
	strangerSocket.Close();
	const std::vector<unsigned char> wrongSlot = RelayBytes(Idle(OtherPlayer, 16), 2);
	Send(serverSocket, wrongSlot);                                 // under the host slot of another device
	EngineReadsAll(200);
	CHECK(Dropped() == 2);
	Send(serverSocket, Idle(OtherPlayer, 16));                     // the bare packet, as the star passed it on
	Send(serverSocket, RelayBytes(QuitPacket(OtherPlayer, 16, LocalPlayer), 0)); // a quit of somebody else in it
	Send(serverSocket, RelayBytes(Idle(LocalPlayer, 40), 0));      // our own packet
	Send(serverSocket, RelayBytes(Idle(7, 16), 0));                // a player who is not in the game
	EngineReadsAll(200);
	CHECK(HasStored(16, OtherPlayer) == false);
	CHECK(HasStored(40, LocalPlayer) == false);
	CHECK(HasStored(16, 7) == false);
	CHECK(NetworkTestPlayerQuit(LocalPlayer) == false);
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync == false);

	// player 0 holds them and passes them on
	for (unsigned long cycle = 16; cycle <= 20; ++cycle) {
		Send(serverSocket, RelayBytes(Idle(OtherPlayer, cycle), 0));
	}
	EngineReadsAll(200);
	for (unsigned long cycle = 16; cycle <= 20; ++cycle) {
		CHECK(StoredType(cycle, OtherPlayer) == MessageSync);
	}
	NetworkTestMeshUpdate();
	CHECK(NetworkInSync);
	// nobody is waited for: he was not heard himself, but his packets are there
	CHECK(NetworkGetWaitState().Player == -1);
	RunCycles();
	CHECK(GameCycle == 20);
	CHECK(NetworkTestPlayerQuit(OtherPlayer) == false);
	// from now on everybody is asked at once for the packets of player 2
	Take(serverSocket);
	bool again = false;
	const unsigned long second = GetTicks();
	while (GetTicks() - second < 500 && !again) {
		EngineReadsAll(5);
		NetworkTestMeshUpdate();
		for (const mesh::Msg &message : Take(serverSocket).meshes) {
			if (message.kind == mesh::Kind::Need && message.cycle == 21 && message.mask == 0x4) {
				CHECK(message.flags == 1);
				again = true;
			}
		}
	}
	CHECK(again);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a relay is no relay without the model")
{
	REQUIRE(started);
	Deliver(15, 15);
	NetworkTestMeshStop();
	Send(serverSocket, RelayBytes(Idle(OtherPlayer, 16), 0));
	EngineReadsAll(200);
	CHECK(HasStored(16, OtherPlayer) == false);
	CHECK(Dropped() == 1);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a player leaves, his quit runs at its cycle")
{
	REQUIRE(started);
	Deliver(20, 15);
	Send(otherSocket, QuitPacket(OtherPlayer, 16, OtherPlayer));
	// a quit of somebody else in the packet of a player is not taken
	Send(serverSocket, QuitPacket(ServerPlayer, 21, LocalPlayer));
	EngineReadsAll(200);
	CHECK(QuitStored(16, OtherPlayer));
	CHECK(QuitReasonStored(16, OtherPlayer) == NetQuitLeft);
	CHECK(HasStored(21, ServerPlayer) == false);
	CHECK(NetworkTestPlayerQuit(LocalPlayer) == false);

	CHECK(RunCycles(15) == 15);
	CHECK(NetworkInSync); // cycle 16 may run: his quit is there, nothing else of his is needed
	CHECK(NetworkGetWaitState().Player == -1);
	REQUIRE(NetworkTestMeshGlue() != nullptr);
	CHECK(NetworkTestMeshGlue()->Model().RemovedAt(2) == 16);
	CHECK(NetworkTestMeshGlue()->Model().RemovedWhy(2) == mesh::Reason::Left);
	// and nothing of his after it: the cycles after 16 need player 0 only
	CHECK(NetworkTestMeshGlue()->Model().LiveMaskAt(17) == 0x3);
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: the local player leaves")
{
	REQUIRE(started);
	Deliver(15, 15);
	CHECK(RunCycles(3) == 3);
	Take(serverSocket);
	Take(otherSocket);

	// the device of player 0 will miss the quit and ask for the cycle it then waits at; its
	// request is on the way while the local player leaves
	mesh::Msg need;
	need.kind = mesh::Kind::Need;
	need.cycle = 14;
	need.mask = uint16_t(1u << 1);
	Send(serverSocket, MeshBytes(need, 0));

	const unsigned long start = GetTicks();
	NetworkQuitGame(); // stays until nobody has asked for 300 ms
	const unsigned long taken = GetTicks() - start;
	CHECK(taken >= 300);
	CHECK(taken < 900); // not up to the cap of 1000 ms
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const Seen server = Take(serverSocket);
	const Seen other = Take(otherSocket);
	for (const Seen *seen : {&server, &other}) {
		// the quit, repeated while the leaver stays; a device which loses the first two has it
		REQUIRE(seen->packets.size() >= 3);
		CHECK(seen->packets.size() <= 6);
		for (const CNetworkPacket &packet : seen->packets) {
			CHECK(packet.Header.Type[0] == MessageQuit);
			CHECK(packet.Header.OrigPlayer == LocalPlayer);
			CHECK(packet.Header.Cycle == 3 + 1 + 10);
			CHECK(QuitReasonOf(packet) == NetQuitLeft);
		}
		CHECK(seen->relays.empty());
	}
	// the one who asked got it once more, as the answer
	CHECK(server.packets.size() == other.packets.size() + 1);
	CHECK(QuitStored(14, LocalPlayer));
	CHECK(QuitReasonStored(14, LocalPlayer) == NetQuitLeft);
	// leaving is no lost connection, and this device takes no part any more
	CHECK(NetworkGetWaitState().SelfOut == false);
	CHECK(NetworkTestMeshGlue() == nullptr);
	// said once per game: the call of the exit path adds nothing
	NetworkQuitGame();
	CHECK(serverSocket.HasDataToRead(150) == false);
	CHECK(otherSocket.HasDataToRead(0) == false);
}

//
// The start barrier (B.4): the game is held until every device has loaded.
//

TEST_CASE_FIXTURE(MeshNetwork, "mesh start: a device which loads its map says that it is alive")
{
	// the room is over, the game is not set up yet (no model): the map loads
	REQUIRE(NetworkTestMeshGlue() == nullptr);
	std::this_thread::sleep_for(std::chrono::milliseconds(260));
	for (int i = 0; i != 20; ++i) {
		NetworkLoadingPing(); // called from every step of the load
	}
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		const std::vector<unsigned char> bytes = ReceiveBytes(*socket);
		mesh::Msg message;
		int from = -1;
		REQUIRE(mesh::Deserialize(bytes.data(), bytes.size(), &message, &from));
		CHECK(message.kind == mesh::Kind::Loading);
		CHECK(from == 1);
		CHECK(socket->HasDataToRead(50) == false); // one in 250 ms
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(260));
	NetworkLoadingPing();
	CHECK(ReceiveBytes(serverSocket).empty() == false);
	CHECK(ReceiveBytes(otherSocket).empty() == false);
}

TEST_CASE_FIXTURE(MeshGameHeld, "mesh start: no cycle and no command before both other devices have loaded")
{
	REQUIRE(started);
	CHECK(heldFromStart);
	CHECK(NetworkIsHeld());
	CHECK(NetworkInSync == false);
	CHECK(RunCycles() == 0);

	// the rest of the load: "still loading" to both, at the rate of the model
	const unsigned long loading = GetTicks();
	while (GetTicks() - loading < 600) {
		NetworkLoadingPing();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		const Seen seen = Take(*socket);
		CHECK(Count(seen, mesh::Kind::Loading) >= 2);
		CHECK(Count(seen, mesh::Kind::Loading) <= 4);
		CHECK(seen.meshes.size() == size_t(Count(seen, mesh::Kind::Loading)));
		CHECK(seen.packets.empty());
	}
	CNetworkWaitState state = NetworkGetWaitState();
	CHECK(state.Held);
	CHECK(state.Loaded == false);
	CHECK(state.NotLoaded == ((1u << ServerPlayer) | (1u << OtherPlayer)));

	// the load is done; the game loop runs its frames, no cycle
	NetworkGameLoaded();
	NetworkLoadingPing(); // not any more
	Frame();
	CHECK(NetworkIsHeld());
	CHECK(RunCycles() == 0);
	CHECK(GameCycle == 0);
	// orders of the local player are not taken
	CUnit unit;
	NetworkSendCommand(MessageCommandStop, unit, 0, 0, nullptr, nullptr, EFlushMode::On);
	NetworkSendExtendedCommand(ExtendedMessageDiplomacy, 1, 2, 3, 4, 0);

	// the device of player 0 has loaded
	Send(serverSocket, MeshBytes(LoadedBy(0x1), 0));
	Frame(100);
	CHECK(NetworkIsHeld());
	CHECK(NetworkInSync == false);
	CHECK(RunCycles() == 0);
	state = NetworkGetWaitState();
	CHECK(state.Held);
	CHECK(state.Loaded);
	CHECK(state.NotLoaded == (1u << OtherPlayer));
	CHECK(state.RemovedAtStart == 0);
	NetworkSendExtendedCommand(ExtendedMessageDiplomacy, 1, 2, 3, 4, 0);
	// command packets of the cycles which are held anyway change nothing
	Send(serverSocket, Idle(ServerPlayer, 5));
	Frame(50);
	CHECK(NetworkIsHeld());
	CHECK(RunCycles() == 0);

	// the device of player 2 has loaded: the game begins
	Send(otherSocket, MeshBytes(LoadedBy(0x4), 2));
	Frame(100);
	CHECK(NetworkIsHeld() == false);
	CHECK(NetworkInSync);
	CHECK(NetworkGetWaitState().Held == false);
	CHECK(RunCycles() == 10);
	CHECK(GameCycle == 10);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		const Seen seen = Take(*socket);
		// our report reached both, and names the devices we knew as loaded
		CHECK(Count(seen, mesh::Kind::Loaded) >= 1);
		CHECK(Count(seen, mesh::Kind::Loading) == 0);
		for (const mesh::Msg &message : seen.meshes) {
			if (message.kind == mesh::Kind::Loaded) {
				CHECK((message.mask & 0x2) != 0);
			}
		}
		// the packets of the first ten cycles: none of the orders given before the start is in them
		REQUIRE(seen.packets.size() == 10);
		for (size_t i = 0; i != seen.packets.size(); ++i) {
			CHECK(seen.packets[i].Header.Cycle == 11 + i);
			CHECK(seen.packets[i].Header.Type[0] == MessageSync);
			CHECK(seen.packets[i].Header.Type[1] == MessageNone);
		}
	}

	// from now on an order is taken: it is in the next packet
	NetworkSendExtendedCommand(ExtendedMessageDiplomacy, 1, 2, 3, 4, 0);
	Deliver(11, 11);
	NetworkTestMeshUpdate();
	CHECK(RunCycles() == 1);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const Seen seen = Take(serverSocket);
	REQUIRE(seen.packets.size() == 1);
	CHECK(seen.packets[0].Header.Cycle == 21);
	CHECK(seen.packets[0].Header.Type[0] == MessageExtendedCommand);
}

TEST_CASE_FIXTURE(MeshGameHeld, "mesh start: a device which never answers is removed at the bound, the others begin")
{
	REQUIRE(started);
	NetworkGameLoaded();
	const unsigned long start = GetTicks();
	// the device of player 0 (the lowest) is dead; this one is the lowest of the rest and names the
	// cycle, once the device of player 2 sees the same
	bool noSignal = false;
	bool removing = false;
	bool committed = false;
	uint32_t sequence = 0;
	while (GetTicks() - start < 3000 && !QuitStored(11, ServerPlayer)) {
		Send(otherSocket, MeshBytes(LoadedBy(0x6), 2));
		Frame();
		const CNetworkWaitState state = NetworkGetWaitState();
		CHECK(state.SelfOut == false);
		if (!QuitStored(11, ServerPlayer)) {
			CHECK(state.Held);
			CHECK(state.NotLoaded == (1u << ServerPlayer));
			CHECK(state.RemovedAtStart == 0);
			CHECK(RunCycles() == 0);
		}
		noSignal = noSignal || state.NoSignal == (1u << ServerPlayer);
		removing = removing || (state.Player == ServerPlayer && state.Removing);
		const Seen seen = Take(otherSocket);
		for (const mesh::Msg &message : seen.meshes) {
			if (message.kind == mesh::Kind::Status && message.player == 1 && !(message.mask & 0x1)) {
				mesh::Msg view = message; // the same view, as the device of player 2 has it
				view.player = 2;
				view.data = ++sequence;
				view.want = 0;
				Send(otherSocket, MeshBytes(view, 2));
			} else if (message.kind == mesh::Kind::Commit) {
				CHECK(message.player == 0);
				CHECK(message.cycle == 11);
				CHECK(message.mask == 0x6);
				committed = true;
			}
		}
	}
	const unsigned long taken = GetTicks() - start;
	REQUIRE(QuitStored(11, ServerPlayer));
	CHECK(taken >= 450); // the bound of a device which says nothing: 500 ms, not the 1500 of a slow one
	CHECK(taken < 1200);
	CHECK(noSignal);
	CHECK(removing);
	CHECK(QuitReasonStored(11, ServerPlayer) == NetQuitNeverStarted);
	CHECK(NetworkTestPlayerQuit(ServerPlayer));
	CHECK(QuitStored(10, ServerPlayer) == false);
	// the game begins with the two which are there
	Frame();
	CHECK(NetworkIsHeld() == false);
	CHECK(NetworkInSync);
	const CNetworkWaitState state = NetworkGetWaitState();
	CHECK(state.Held == false);
	CHECK(state.RemovedAtStart == (1u << ServerPlayer));
	CHECK(RunCycles() == 10);
	CHECK(GameCycle == 10); // cycle 11, with his quit in it, is the next one
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	for (const mesh::Msg &message : Take(otherSocket).meshes) {
		committed = committed || (message.kind == mesh::Kind::Commit && message.player == 0 && message.cycle == 11);
	}
	CHECK(committed);
	// the removed device is told as well, should it still come
	bool told = false;
	for (const mesh::Msg &message : Take(serverSocket).meshes) {
		told = told || (message.kind == mesh::Kind::Commit && message.player == 0 && message.cycle == 11);
	}
	CHECK(told);
}

TEST_CASE_FIXTURE(MeshGameHeld, "mesh start: a device which still loads is waited for beyond the short bound")
{
	REQUIRE(started);
	NetworkGameLoaded();
	Send(otherSocket, MeshBytes(LoadedBy(0x6), 2));
	mesh::Msg loading;
	loading.kind = mesh::Kind::Loading;
	const unsigned long start = GetTicks();
	// twice the short bound: the device of player 0 says that it loads, nobody gives it up
	while (GetTicks() - start < 1000) {
		Send(serverSocket, MeshBytes(loading, 0));
		Send(otherSocket, MeshBytes(LoadedBy(0x6), 2));
		Frame();
	}
	CNetworkWaitState state = NetworkGetWaitState();
	CHECK(state.Held);
	CHECK(state.NotLoaded == (1u << ServerPlayer));
	CHECK(state.NoSignal == 0);
	CHECK(state.Player == ServerPlayer);
	CHECK(state.Removing == false);
	CHECK(state.MsLeftOf[ServerPlayer] > 0);
	CHECK(state.MsLeftOf[ServerPlayer] <= 1500);
	CHECK(NetworkTestPlayerQuit(ServerPlayer) == false);
	CHECK(RunCycles() == 0);
	for (const mesh::Msg &message : Take(otherSocket).meshes) {
		CHECK(message.kind != mesh::Kind::Status);
	}
	// it finishes: the game begins with all three
	Send(serverSocket, MeshBytes(LoadedBy(0x1), 0));
	Frame(100);
	CHECK(NetworkIsHeld() == false);
	CHECK(NetworkGetWaitState().RemovedAtStart == 0);
	CHECK(RunCycles() == 10);
}

TEST_CASE_FIXTURE(MeshGameHeld, "mesh start: the local player leaves before the game has begun")
{
	REQUIRE(started);
	NetworkGameLoaded();
	Frame();
	Take(serverSocket);
	Take(otherSocket);
	NetworkQuitGame();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		const Seen seen = Take(*socket);
		REQUIRE(seen.packets.empty() == false);
		CHECK(seen.packets[0].Header.Type[0] == MessageQuit);
		CHECK(seen.packets[0].Header.Cycle == 11); // the first cycle which is not held from the start
		CHECK(QuitReasonOf(seen.packets[0]) == NetQuitLeft);
	}
}

TEST_CASE_FIXTURE(MeshGame, "mesh game: a pause for everybody begins with the cycle of its command and ends by a message")
{
	REQUIRE(started);
	GamePaused = false;
	Deliver(40, 40);
	CHECK(RunCycles(5) == 5);
	Take(serverSocket);
	Take(otherSocket);

	// the local player presses Pause: nothing stands yet, the command leaves with the next packet
	CHECK(NetworkTogglePause());
	CHECK(NetworkIsPaused() == false);
	CHECK(GamePaused == false);
	CHECK(RunCycles() == 11); // the cycles 6..16: the command lies in cycle 6 + lag
	CHECK(GameCycle == 16);
	CHECK(NetworkIsPaused());
	CHECK(GamePaused);
	CHECK(NetworkInSync == false);
	CNetworkWaitState state = NetworkGetWaitState();
	CHECK(state.Paused);
	CHECK(state.PausedBy == LocalPlayer);
	CHECK(state.Player == -1);
	CHECK(StoredType(16, LocalPlayer) == MessageExtendedCommand);

	// a window which closes lets the game run again; not this pause
	GamePaused = false;
	NetworkRecover();
	CHECK(GamePaused);
	CHECK(NetworkInSync == false);
	CHECK(RunCycles() == 0);
	// the standing device says so to both others
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	NetworkRecover();
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		bool stands = false;
		for (const mesh::Msg &message : Take(*socket).meshes) {
			stands = stands || (message.kind == mesh::Kind::Pause && message.flags == 1 && message.cycle == 16 && message.player == 1);
		}
		CHECK(stands);
	}

	// the player of host slot 2 lets the game go on
	mesh::Msg over;
	over.kind = mesh::Kind::Pause;
	over.player = 2;
	over.cycle = 16;
	Send(otherSocket, MeshBytes(over, 2));
	EngineReadsAll(200);
	CHECK(NetworkIsPaused() == false);
	CHECK(GamePaused == false);
	NetworkRecover();
	CHECK(NetworkInSync);
	CHECK(NetworkGetWaitState().Paused == false);

	// the player of host slot 2 pauses: his command lies in his packet of cycle 45
	for (unsigned long cycle = 41; cycle <= 60; ++cycle) {
		Send(serverSocket, Idle(ServerPlayer, cycle));
		if (cycle == 45) {
			CNetworkExtendedCommand nec;
			nec.ExtendedType = ExtendedMessagePause;
			nec.Arg2 = OtherPlayer;
			std::vector<unsigned char> data(nec.Size());
			nec.Serialize(data.data());
			Send(otherSocket, BuildPacket(OtherPlayer, cycle, MessageExtendedCommand, data));
		} else {
			Send(otherSocket, Idle(OtherPlayer, cycle));
		}
	}
	EngineReadsAll(200);
	CHECK(RunCycles() == 29); // 17..45
	CHECK(GameCycle == 45);
	CHECK(NetworkIsPaused());
	CHECK(GamePaused);
	CHECK(NetworkGetWaitState().PausedBy == OtherPlayer);
	Take(serverSocket);
	Take(otherSocket);

	// the local player presses the button again: the game goes on, the others are told
	CHECK(NetworkTogglePause());
	CHECK(NetworkIsPaused() == false);
	CHECK(GamePaused == false);
	CHECK(NetworkInSync);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	for (CUDPSocket *socket : {&serverSocket, &otherSocket}) {
		bool told = false;
		for (const mesh::Msg &message : Take(*socket).meshes) {
			told = told || (message.kind == mesh::Kind::Pause && message.flags == 0 && message.cycle == 45 && message.player == 1);
		}
		CHECK(told);
	}
	CHECK(RunCycles() == 15); // 46..60
}

TEST_CASE_FIXTURE(MeshGameHeld, "mesh start: no pause before the game has begun")
{
	REQUIRE(started);
	GamePaused = false;
	CHECK(NetworkTogglePause() == false);
	CHECK(NetworkIsPaused() == false);
	CHECK(GamePaused == false);
}

TEST_CASE_FIXTURE(InGameNetworkWithSockets, "mesh off: the pause button does nothing")
{
	GamePaused = false;
	CHECK(NetworkTogglePause() == false);
	NetworkExecPause(0);
	CHECK(NetworkIsPaused() == false);
	CHECK(GamePaused == false);
}
