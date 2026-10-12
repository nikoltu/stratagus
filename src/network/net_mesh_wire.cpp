// net_mesh_wire — see net_mesh_wire.h.
//
//   kind     fields after the 4-byte head (bytes)                                        total
//   Cmd      player 1, flags 1, cycle 4, data 4                                             14
//   Loading  —                                                                               4
//   Loaded   mask 2                                                                          6
//   Need     flags 1, cycle 4, mask 2                                                       11
//   Status   player 1, data 4, mask 2, removed 2, want 2, rounds 8 x 2, have 8 x 4          63
//   Commit   player 1, flags 1, round 2, cycle 4, mask 2                                    14
//   Cancel   player 1, round 2                                                               7
//   Pause    player 1, flags 1, cycle 4                                                     10
//   Chat     data 4, length 1, text 1..240 (SerializeChat, DeserializeChat)            10..249
//   ChatAck  data 4                                                                          8
//
// Ranges: sender and player below kMaxPlayers; Cmd, Need and Pause flags 0 or 1; Commit flags a Reason;
// cycles 0..kWireMaxCycle; every `have` -1..kWireMaxCycle. Masks are taken as they are: the model
// itself sends 0xFFFF as "no alive set" in the commit of a player who left.
#include "net_mesh_wire.h"

#include "net_message.h"
#include "net_serialization.h"

#include <cstring>

namespace mesh {

size_t WireSize(Kind kind)
{
	switch (kind) {
	case Kind::Cmd: return kWireCmdSize;
	case Kind::Loading: return kWireLoadingSize;
	case Kind::Loaded: return kWireLoadedSize;
	case Kind::Need: return kWireNeedSize;
	case Kind::Status: return kWireStatusSize;
	case Kind::Commit: return kWireCommitSize;
	case Kind::Cancel: return kWireCancelSize;
	case Kind::Pause: return kWirePauseSize;
	case Kind::Chat: return 0; // not of one length: SerializeChat
	case Kind::ChatAck: return kWireChatAckSize;
	}
	return 0;
}

// The one place that says what a valid message is; both directions go through it.
static bool Valid(const Msg &m, int from)
{
	const bool cycle = m.cycle >= 0 && m.cycle <= kWireMaxCycle;
	if (from < 0 || from >= kMaxPlayers) return false;
	switch (m.kind) {
	case Kind::Cmd:
		return m.player < kMaxPlayers && m.flags <= 1 && cycle;
	case Kind::Loading:
	case Kind::Loaded:
		return true;
	case Kind::Need:
		return m.flags <= 1 && cycle;
	case Kind::Status:
		if (m.player >= kMaxPlayers) return false;
		for (int p = 0; p < kMaxPlayers; ++p) {
			if (m.have[p] < -1 || m.have[p] > kWireMaxCycle) return false;
		}
		return true;
	case Kind::Commit:
		return m.player < kMaxPlayers && m.flags <= uint8_t(Reason::Desync) && cycle;
	case Kind::Cancel:
		return m.player < kMaxPlayers;
	case Kind::Pause:
		return m.player < kMaxPlayers && m.flags <= 1 && cycle;
	case Kind::Chat:
		return false;
	case Kind::ChatAck:
		return true; // an answer to the number 0, which no line has, answers nothing
	}
	return false;
}

size_t Serialize(const Msg &m, int from, unsigned char *buf, size_t cap)
{
	const size_t size = WireSize(m.kind);
	if (buf == nullptr || size == 0 || cap < size || !Valid(m, from)) return 0;
	unsigned char *p = buf;
	p += serialize8(p, uint8_t(MessageMesh));
	p += serialize8(p, kWireVersion);
	p += serialize8(p, uint8_t(m.kind));
	p += serialize8(p, uint8_t(from));
	switch (m.kind) {
	case Kind::Cmd:
		p += serialize8(p, m.player);
		p += serialize8(p, m.flags);
		p += serialize32(p, m.cycle);
		p += serialize32(p, m.data);
		break;
	case Kind::Loading:
		break;
	case Kind::Loaded:
		p += serialize16(p, m.mask);
		break;
	case Kind::Need:
		p += serialize8(p, m.flags);
		p += serialize32(p, m.cycle);
		p += serialize16(p, m.mask);
		break;
	case Kind::Status:
		p += serialize8(p, m.player);
		p += serialize32(p, m.data);
		p += serialize16(p, m.mask);
		p += serialize16(p, m.removed);
		p += serialize16(p, m.want);
		for (int i = 0; i < kMaxPlayers; ++i) p += serialize16(p, m.rounds[i]);
		for (int i = 0; i < kMaxPlayers; ++i) p += serialize32(p, m.have[i]);
		break;
	case Kind::Commit:
		p += serialize8(p, m.player);
		p += serialize8(p, m.flags);
		p += serialize16(p, m.round);
		p += serialize32(p, m.cycle);
		p += serialize16(p, m.mask);
		break;
	case Kind::Cancel:
		p += serialize8(p, m.player);
		p += serialize16(p, m.round);
		break;
	case Kind::Pause:
		p += serialize8(p, m.player);
		p += serialize8(p, m.flags);
		p += serialize32(p, m.cycle);
		break;
	case Kind::Chat:
		break;
	case Kind::ChatAck:
		p += serialize32(p, m.data);
		break;
	}
	return size;
}

bool Deserialize(const unsigned char *buf, size_t len, Msg *m, int *from)
{
	if (buf == nullptr || m == nullptr || from == nullptr) return false;
	if (len < kWireHeaderSize || len > kWireMaxSize) return false;
	if (buf[0] != uint8_t(MessageMesh) || buf[1] != kWireVersion) return false;
	if (buf[2] > uint8_t(Kind::ChatAck)) return false;
	Msg r;
	r.kind = Kind(buf[2]); // the probe, its answer and the relay are no kinds, a chat line has its own parser: their size is 0
	if (len != WireSize(r.kind)) return false; // from here every read below is inside the datagram
	const int sender = buf[3];
	const unsigned char *p = buf + kWireHeaderSize;
	switch (r.kind) {
	case Kind::Cmd:
		p += deserialize8(p, &r.player);
		p += deserialize8(p, &r.flags);
		p += deserialize32(p, &r.cycle);
		p += deserialize32(p, &r.data);
		break;
	case Kind::Loading:
		break;
	case Kind::Loaded:
		p += deserialize16(p, &r.mask);
		break;
	case Kind::Need:
		p += deserialize8(p, &r.flags);
		p += deserialize32(p, &r.cycle);
		p += deserialize16(p, &r.mask);
		break;
	case Kind::Status:
		p += deserialize8(p, &r.player);
		p += deserialize32(p, &r.data);
		p += deserialize16(p, &r.mask);
		p += deserialize16(p, &r.removed);
		p += deserialize16(p, &r.want);
		for (int i = 0; i < kMaxPlayers; ++i) p += deserialize16(p, &r.rounds[i]);
		for (int i = 0; i < kMaxPlayers; ++i) p += deserialize32(p, &r.have[i]);
		break;
	case Kind::Commit:
		p += deserialize8(p, &r.player);
		p += deserialize8(p, &r.flags);
		p += deserialize16(p, &r.round);
		p += deserialize32(p, &r.cycle);
		p += deserialize16(p, &r.mask);
		break;
	case Kind::Cancel:
		p += deserialize8(p, &r.player);
		p += deserialize16(p, &r.round);
		break;
	case Kind::Pause:
		p += deserialize8(p, &r.player);
		p += deserialize8(p, &r.flags);
		p += deserialize32(p, &r.cycle);
		break;
	case Kind::Chat:
		break;
	case Kind::ChatAck:
		p += deserialize32(p, &r.data);
		break;
	}
	if (size_t(p - buf) != len || !Valid(r, sender)) return false;
	*m = r;
	*from = sender;
	return true;
}

size_t SerializeProbe(bool ack, int from, unsigned char *buf, size_t cap)
{
	if (buf == nullptr || cap < kWireProbeSize || from < 0 || from >= kMaxPlayers) return 0;
	unsigned char *p = buf;
	p += serialize8(p, uint8_t(MessageMesh));
	p += serialize8(p, kWireVersion);
	p += serialize8(p, ack ? kWireProbeAck : kWireProbe);
	p += serialize8(p, uint8_t(from));
	return kWireProbeSize;
}

bool DeserializeProbe(const unsigned char *buf, size_t len, bool *ack, int *from)
{
	if (buf == nullptr || ack == nullptr || from == nullptr) return false;
	if (len != kWireProbeSize) return false;
	if (buf[0] != uint8_t(MessageMesh) || buf[1] != kWireVersion) return false;
	if (buf[2] != kWireProbe && buf[2] != kWireProbeAck) return false;
	if (buf[3] >= kMaxPlayers) return false;
	*ack = buf[2] == kWireProbeAck;
	*from = buf[3];
	return true;
}

size_t SerializeRelay(const unsigned char *packet, size_t len, int from, unsigned char *buf, size_t cap)
{
	if (buf == nullptr || packet == nullptr || len == 0 || from < 0 || from >= kMaxPlayers) return 0;
	if (len > kWireRelayMaxSize - kWireHeaderSize || cap < kWireHeaderSize + len) return 0;
	unsigned char *p = buf;
	p += serialize8(p, uint8_t(MessageMesh));
	p += serialize8(p, kWireVersion);
	p += serialize8(p, kWireRelay);
	p += serialize8(p, uint8_t(from));
	std::memcpy(p, packet, len);
	return kWireHeaderSize + len;
}

bool DeserializeRelay(const unsigned char *buf, size_t len, int *from, const unsigned char **packet, size_t *packetLen)
{
	if (buf == nullptr || from == nullptr || packet == nullptr || packetLen == nullptr) return false;
	if (len <= kWireHeaderSize || len > kWireRelayMaxSize) return false;
	if (buf[0] != uint8_t(MessageMesh) || buf[1] != kWireVersion || buf[2] != kWireRelay) return false;
	if (buf[3] >= kMaxPlayers) return false;
	*from = buf[3];
	*packet = buf + kWireHeaderSize;
	*packetLen = len - kWireHeaderSize;
	return true;
}

static bool ChatTextValid(const unsigned char *text, size_t len)
{
	if (len == 0 || len > kWireChatMaxText) return false;
	for (size_t i = 0; i < len; ++i) {
		if (text[i] < 0x20) return false;
	}
	return true;
}

size_t SerializeChat(uint32_t number, const char *text, size_t textLen, int from, unsigned char *buf, size_t cap)
{
	if (buf == nullptr || text == nullptr || number == 0 || from < 0 || from >= kMaxPlayers) return 0;
	if (!ChatTextValid(reinterpret_cast<const unsigned char *>(text), textLen)) return 0;
	if (cap < kWireChatHeadSize + textLen) return 0;
	unsigned char *p = buf;
	p += serialize8(p, uint8_t(MessageMesh));
	p += serialize8(p, kWireVersion);
	p += serialize8(p, uint8_t(Kind::Chat));
	p += serialize8(p, uint8_t(from));
	p += serialize32(p, number);
	p += serialize8(p, uint8_t(textLen));
	std::memcpy(p, text, textLen);
	return kWireChatHeadSize + textLen;
}

bool DeserializeChat(const unsigned char *buf, size_t len, int *from, uint32_t *number,
                     const char **text, size_t *textLen)
{
	if (buf == nullptr || from == nullptr || number == nullptr || text == nullptr || textLen == nullptr) return false;
	if (len <= kWireChatHeadSize || len > kWireChatMaxSize) return false;
	if (buf[0] != uint8_t(MessageMesh) || buf[1] != kWireVersion || buf[2] != uint8_t(Kind::Chat)) return false;
	if (buf[3] >= kMaxPlayers) return false;
	uint32_t n = 0;
	deserialize32(buf + kWireHeaderSize, &n);
	const size_t size = buf[kWireHeaderSize + 4];
	if (n == 0 || len != kWireChatHeadSize + size || !ChatTextValid(buf + kWireChatHeadSize, size)) return false;
	*from = buf[3];
	*number = n;
	*text = reinterpret_cast<const char *>(buf + kWireChatHeadSize);
	*textLen = size;
	return true;
}

} // namespace mesh
