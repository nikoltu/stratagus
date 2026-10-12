// net_mesh_wire — the bytes of a net_mesh message (LAN_MESH_SPEC.md part B.2, as changed by B.9).
//
// One message = one UDP datagram:
//
//   byte 0  MessageMesh (net_message.h) — the first byte tells the datagram from every other kind
//   byte 1  kWireVersion
//   byte 2  Kind
//   byte 3  the sender's player number (the receiver still checks it against the source address)
//   then the fields of the kind, fixed width, in the byte order of every other engine message
//   (net_serialization.h: network order, high byte first).
//
// Every kind has exactly one length. A datagram of any other length, with another first byte,
// version or kind, or with a field outside its range, is refused as a whole: nothing of it is used.
// The parser reads only inside [buf, buf + len), allocates nothing and throws nothing.
//
// "Player" on the wire — the sender byte, every player field, every bit of a mask — is the HOST
// SLOT: the index 0..kMaxPlayers-1 in Hosts[] of the table the server sent with Config, which is
// the same on every device of a game. It is not the game player index (Hosts[slot].PlyNr, up to
// PlayerMax-1). network.cpp translates at the boundary: slot -> Hosts[slot].PlyNr on receipt,
// the own slot (NetLocalHostsSlot) as sender.
//
// The probe of the setup (B.1.4) is no kind of the model. It has the same 4-byte head with a kind
// byte above the model's kinds and no body; Deserialize refuses it, DeserializeProbe takes it.
//
// This file needs the engine (the type byte and the serialisation helpers); net_mesh.h does not,
// and must stay that way: the simulator builds the model alone.
#pragma once

#include <cstddef>
#include <cstdint>

#include "net_mesh.h"

namespace mesh {

constexpr uint8_t kWireVersion = 1;

// The highest cycle number a datagram may name. Below kNever by a margin, so that no sum of a
// received cycle and a small number (the lag, a quit's own cycle) can overflow in the model.
// At 30 cycles a second it is reached after more than two years.
constexpr int32_t kWireMaxCycle = kNever - 0x10000;

constexpr size_t kWireHeaderSize = 1 + 1 + 1 + 1; // type, version, kind, sender

constexpr size_t kWireCmdSize = kWireHeaderSize + 1 + 1 + 4 + 4;     // player, flags, cycle, data
constexpr size_t kWireLoadingSize = kWireHeaderSize;
constexpr size_t kWireLoadedSize = kWireHeaderSize + 2;              // mask
constexpr size_t kWireNeedSize = kWireHeaderSize + 1 + 4 + 2;        // flags, cycle, mask
constexpr size_t kWireStatusSize = kWireHeaderSize + 1 + 4 + 2 + 2 + 2 // player, data, mask, removed, want
	+ kMaxPlayers * 2 + kMaxPlayers * 4;                             // rounds, have
constexpr size_t kWireCommitSize = kWireHeaderSize + 1 + 1 + 2 + 4 + 2; // player, flags, round, cycle, mask
constexpr size_t kWireCancelSize = kWireHeaderSize + 1 + 2;          // player, round
constexpr size_t kWirePauseSize = kWireHeaderSize + 1 + 1 + 4;       // player, flags, cycle
constexpr size_t kWireChatAckSize = kWireHeaderSize + 4;             // data

constexpr size_t kWireMaxSize = kWireStatusSize; // 63: the longest datagram of any kind

// Length of a datagram of that kind; 0 for a value that is not a kind.
size_t WireSize(Kind kind);

// Writes the datagram of `m` as sent by player `from`. Returns its length, or 0 — and writes
// nothing — when `cap` is too small or a field is outside the range the parser accepts. Only the
// fields of the kind travel (see the table in the .cpp); the others are not sent.
size_t Serialize(const Msg &m, int from, unsigned char *buf, size_t cap);

// Reads one datagram. Returns false, leaving `*m` and `*from` as they were, unless the whole
// datagram is valid. On success the fields the kind does not carry are zero.
bool Deserialize(const unsigned char *buf, size_t len, Msg *m, int *from);

// The kind bytes of the probe and of its answer; the datagram is the head alone. Kind::Pause
// (10) lies above them and above the relay.
constexpr uint8_t kWireProbe = 7;
constexpr uint8_t kWireProbeAck = 8;
constexpr size_t kWireProbeSize = kWireHeaderSize;

// Writes a probe (or its answer) of host slot `from`. Returns its length, or 0 — and writes
// nothing — when `cap` is too small or `from` is no slot.
size_t SerializeProbe(bool ack, int from, unsigned char *buf, size_t cap);

// Reads a probe or its answer. Returns false, leaving `*ack` and `*from` as they were, unless the
// datagram is exactly one of the two.
bool DeserializeProbe(const unsigned char *buf, size_t len, bool *ack, int *from);

// A relayed command packet (B.3): the head with this kind byte, then the engine's command packet
// as its origin sent it (CNetworkPacket, its header names the origin). It is no kind of the model
// either: the model's Cmd stands for the packet, the engine sends the packet itself. A command
// packet without this head is taken from the address of its own player only.
constexpr uint8_t kWireRelay = 9;
constexpr size_t kWireRelayMaxSize = 1024; // the receive buffer of NetworkEvent

// Writes the relay of the packet bytes [packet, packet + len) as sent by host slot `from`. Returns
// the length, or 0 � and writes nothing � when `cap` is too small, the datagram would be longer
// than kWireRelayMaxSize, the packet is empty or `from` is no slot.
size_t SerializeRelay(const unsigned char *packet, size_t len, int from, unsigned char *buf, size_t cap);

// Reads a relay. Returns false, leaving the output arguments as they were, unless the datagram is
// one; on success [*packet, *packet + *len) lies inside the datagram.
bool DeserializeRelay(const unsigned char *buf, size_t len, int *from, const unsigned char **packet, size_t *packetLen);

// A chat line (Kind::Chat): the head, the number of the line (4), the length of the text (1) and
// the text. The one kind whose length is not fixed, so it has its own two functions; Serialize
// and Deserialize refuse it. The text is 1..kWireChatMaxText bytes, none of them below 0x20
// (no line ends, no controls); what the bytes mean (UTF-8) is not the wire's business.
constexpr size_t kWireChatMaxText = 240;
constexpr size_t kWireChatHeadSize = kWireHeaderSize + 4 + 1;
constexpr size_t kWireChatMaxSize = kWireChatHeadSize + kWireChatMaxText;

// Writes the chat line `number` of host slot `from`. Returns the length, or 0 and writes
// nothing when `cap` is too small, the number is 0, `from` is no slot or the text is not valid.
size_t SerializeChat(uint32_t number, const char *text, size_t textLen, int from, unsigned char *buf, size_t cap);

// Reads a chat line. Returns false, leaving the output arguments as they were, unless the
// datagram is one; on success [*text, *text + *textLen) lies inside the datagram.
bool DeserializeChat(const unsigned char *buf, size_t len, int *from, uint32_t *number,
                     const char **text, size_t *textLen);

} // namespace mesh
