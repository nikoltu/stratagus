// net_mesh — the per-device view of a mesh lockstep game (LAN_MESH_SPEC.md part B).
//
// A pure state machine: no sockets, no clock, no globals, no engine types. The caller feeds it
// received messages and the time in milliseconds, drains the outgoing queue and the event queue,
// and asks before every game cycle whether that cycle may run.
//
//   engine main loop:   mesh.Tick(now);  while (mesh.CanExecute(c)) { run c; mesh.OnExecuted(c); }
//   engine UDP socket:  mesh.OnMessage(fromPlayer, msg, now);   send everything in mesh.out
//
// Lockstep rule (spec A.3, B.5 fact F1): the packet a device makes after cycle c carries its
// commands for cycle c + kLag + 1; a device executes cycle c only when it holds the packet of
// every player still in the game at c. Cycles 0..kLag are pre-filled empty (spec A.2 step 7).
#pragma once

#include <cstdint>
#include <vector>

namespace mesh {

constexpr int kMaxPlayers = 8;
constexpr int kLag = 10;
constexpr int32_t kNever = 0x7fffffff;

enum class Kind : uint8_t {
	Cmd,     // one player's command packet of one cycle; player != sender means a relay (B.3)
	Loading, // "still loading" ping (B.4 step 6)
	Loaded,  // map load finished; mask = peers whose Loaded the sender holds (B.4 step 1)
	Need,    // cycle + mask of players whose packet is missing + relay flag (B.3)
	Status,  // the sender's whole view: the spec's `Suspect`, one message for all suspects (B.5)
	Commit,  // player, cycle C, alive mask, reason (B.5 rules 5, 6, 10)
	Cancel,  // player, round (B.5 rule 7)
	// NET_PAUSE_SPEC.md. The values 7, 8 and 9 are the probe, its answer and the relay of the wire.
	Pause = 10, // cycle P of a pause. flags 1: the sender stands after P, player = who paused.
	            // flags 0: that pause is over, player = who let the game go on
	// LAN_CHAT_NOTE.md. A chat line is no part of the game: it has no cycle and is shown on arrival.
	Chat = 11,   // one chat line of the sender; data = its number. The text is not the model's: it
	             // travels with the message (the caller keeps it by the number)
	ChatAck = 12 // data = the number of a line of the receiver that the sender has taken
};

// A device makes no new chat line while its oldest unanswered one is this many numbers back; so
// a receiver that remembers the last 32 numbers of a sender never takes a line for an old one.
constexpr uint32_t kChatWindow = 16;

enum class Reason : uint8_t {
	Lost,           // removed by a removal round (B.5)
	Left,           // deliberate quit (B.6)
	ConnectionLost, // this device was removed by the others (B.5 rule 10, B.4 step 7)
	Isolated,       // this device hears nobody in a game of three or more (B.5 rule 11)
	Desync          // this device executed a cycle the others decided differently; it must stop
};

struct Msg {
	Kind kind = Kind::Cmd;
	uint8_t player = 0;   // Cmd: origin. Commit / Cancel: subject. Status: whose view this is. Pause: who
	uint8_t flags = 0;    // Cmd: 1 = quit. Need: 1 = relay. Commit: Reason. Pause: 1 = stands, 0 = over
	uint16_t round = 0;   // Commit / Cancel
	int32_t cycle = 0;    // Cmd, Need, Commit (C), Pause (P)
	uint32_t data = 0;    // Cmd: the opaque command. Status: sequence number of the view.
	                      // Chat, ChatAck: number of the line
	uint16_t mask = 0;    // Loaded: seen. Need: who. Status: alive. Commit: alive set of the commit
	uint16_t removed = 0; // Status: players the sender has removed
	uint16_t want = 0;    // Status: peers whose current view the sender lacks (asks for a relay)
	uint16_t rounds[kMaxPlayers] = {}; // Status: round per player
	int32_t have[kMaxPlayers] = {};   // Status: highest cycle held without a gap, per player
};

struct Out {
	int to;
	Msg msg;
};

struct Event {
	enum Type : uint8_t { Started, Waiting, PlayerRemoved, SelfOut, Paused, Resumed, ChatLine } type;
	int player;     // Waiting, PlayerRemoved. Paused: who paused. Resumed: who let the game go on.
	                // ChatLine: who said it
	int32_t cycle;  // PlayerRemoved: the removal cycle C. Paused, Resumed: the pause cycle P.
	                // ChatLine: the number of the line (a new one of that player, told once)
	Reason reason;  // PlayerRemoved, SelfOut
	int32_t msLeft; // Waiting: time to the bound
};

struct Params {
	int64_t dropMs = 60000;          // B.8: bound of one absence
	int64_t allowanceMs = 120000;    // B.8: total absence per player per game
	int64_t exhaustedDropMs = 3000;  // B.8: bound once the allowance is used ("out at once")
	int64_t plateMs = 3000;          // B.5 rule 1: silence shown, and counted as an absence, from here
	int64_t hearMs = 5000;           // B.5 rules 3, 7, 8: "heard recently", grace after a cancel
	int64_t loadMs = 60000;          // B.4 step 5: bound for a live slow loader
	int64_t loadPingDropMs = 20000;  // B.4 step 6: bound for a loader that stopped pinging
	int64_t loadedMs = 250;          // B.4 step 1: Loaded / Loading interval
	int64_t needMs = 100;            // B.3: Need interval while stalled
	int64_t relayAfterMs = 500;      // B.3: stall time before the relay flag
	int64_t statusMs = 250;          // B.5 rule 2: Status interval while a round is open
	int64_t lingerQuietMs = 500;     // B.6 step 2
	int64_t lingerCapMs = 2000;      // B.6 step 2
	int64_t chatMs = 250;            // a chat line is sent again at this interval until it is answered
};

class Mesh
{
public:
	Mesh(int players, int me, const Params &params, int64_t now);

	// --- input ---
	void OnLoaded(int64_t now);                          // own map load finished (B.4)
	void OnMessage(int from, const Msg &m, int64_t now); // `from` = the player owning the source address
	void Tick(int64_t now);
	void SetCommand(uint32_t command) { pending_ = command; } // goes into the next own packet
	bool CanExecute(int32_t cycle) const;
	void OnExecuted(int32_t cycle);
	void Quit(int64_t now);                              // deliberate leave (B.6)
	// NET_PAUSE_SPEC.md: a pause command of player `by` ran in the cycle executed last. False if
	// it is ignored (the game has not begun, or it crossed a pause that began lag + 1 cycles ago
	// or less) - the same answer on every device.
	bool Pause(int by, int64_t now);
	void Resume(int64_t now);                            // the local player lets the game go on
	// The local player says a chat line: it goes to every peer now and again until each has
	// answered or is removed. Returns its number (from 1), or 0 if no line is taken: this device
	// is out or has left, or kChatWindow lines back one is still unanswered.
	uint32_t Chat(int64_t now);

	// --- output: the caller drains both ---
	std::vector<Out> out;
	std::vector<Event> events;

	// --- facts for the caller (store stand-in) ---
	int32_t Executed() const { return executed_; }
	bool IsOut() const { return out_; }
	bool IsReleased() const { return released_; }
	bool IsLoaded() const { return loaded_; }
	bool IsLingering() const { return lingering_ && !out_; }   // B.6 step 2: left, still answering
	bool IsPaused() const { return paused_; }                  // no cycle runs: the game is paused for all
	int PausedBy() const { return pausedBy_; }                 // who paused, while IsPaused
	int32_t PauseCycle() const { return pauseCycle_; }         // the cycle the pause began after, while IsPaused
	uint16_t LoadedMask() const { return loadedFrom_; }        // B.4: players known as loaded, the own bit always set
	// B.5 rule 1: p is silent long enough to be shown so.
	bool IsQuiet(int p, int64_t now) const { return now - lastHeard_[p] >= P.plateMs; }
	uint16_t LiveMaskAt(int32_t cycle) const;  // players whose commands run at that cycle
	uint32_t Command(int p, int32_t cycle) const { return cmd_[p][cycle]; }
	int32_t RemovedAt(int p) const { return removedAt_[p]; }
	Reason RemovedWhy(int p) const { return reason_[p]; }
	bool IsSuspected(int p) const { return suspected_[p]; }
	// B.8: what is left of p's allowance, the absence that runs now included.
	int64_t AllowanceLeft(int p, int64_t now) const
	{
		const int64_t gap = now - lastHeard_[p];
		const int64_t rest = P.allowanceMs - used_[p] - (gap >= P.plateMs ? gap : 0);
		return rest > 0 ? rest : 0;
	}
	int conflicts = 0; // commits received that contradict an applied one (must stay 0)

private:
	bool Alive(int p) const { return removedAt_[p] == kNever && !suspected_[p]; }
	uint16_t AliveMask() const;
	uint16_t RemovedMask() const;
	bool Hold(int p, int32_t c) const { return c >= 0 && c < (int32_t)st_[p].size() && st_[p][c] != 0; }
	bool Put(int p, int32_t cycle, uint32_t data, bool quit, int64_t now);
	void Remove(int p, int32_t cycle, Reason why, uint16_t mask, int64_t now);
	void Suspect(int p, int64_t now);
	void Unfreeze(int p, uint16_t round, int64_t now);
	void SelfOut(Reason why);
	bool BoundExpired(int p, int64_t now, int64_t *left) const;
	bool HeardRecently(int p, int64_t now) const;
	int64_t DropFor(int p) const;
	bool ViewEqual(const Msg &s, uint16_t alive) const;
	Msg MakeStatus();
	void Send(int to, const Msg &m) { out.push_back({to, m}); }
	void SendPeers(const Msg &m, bool alsoSuspects);
	void SendCommit(int to, int p);
	void AnswerNeed(int from, const Msg &m);
	void EndPause(int32_t cycle, int by);
	bool TakeChat(int from, uint32_t number);

	const int n_;
	const int me_;
	const Params P;

	// store: command and state (0 none, 1 command, 2 quit) per player per cycle
	std::vector<uint32_t> cmd_[kMaxPlayers];
	std::vector<uint8_t> st_[kMaxPlayers];
	int32_t have_[kMaxPlayers];
	int32_t executed_ = -1;
	int32_t nextOwn_ = kLag + 1;
	uint32_t pending_ = 0;

	// membership
	int32_t removedAt_[kMaxPlayers];
	Reason reason_[kMaxPlayers];
	uint16_t commitMask_[kMaxPlayers];
	bool suspected_[kMaxPlayers] = {};
	bool doomed_[kMaxPlayers] = {};
	bool viaRelay_[kMaxPlayers] = {};
	uint16_t round_[kMaxPlayers] = {};
	int64_t suspectSince_[kMaxPlayers] = {};
	int64_t graceUntil_[kMaxPlayers] = {};
	int64_t lastHeard_[kMaxPlayers];
	int64_t lastAdvance_[kMaxPlayers];
	int64_t used_[kMaxPlayers] = {};
	int64_t lastWait_[kMaxPlayers] = {};
	int64_t lastTold_[kMaxPlayers] = {};

	// the latest view received from each peer
	Msg snap_[kMaxPlayers];
	bool snapValid_[kMaxPlayers] = {};
	int64_t snapAt_[kMaxPlayers] = {};
	uint32_t seq_ = 0;
	uint16_t want_ = 0;
	uint16_t replyMask_ = 0;
	int64_t lastStatus_ = 0;

	// start barrier
	bool loaded_ = false;
	bool released_ = false;
	int64_t loadedAt_ = 0;
	uint16_t loadedFrom_ = 0;
	int64_t lastLoadedSent_ = -1000000;

	// stall
	int32_t stallCycle_ = -1;
	int64_t stallSince_ = 0;
	int64_t lastNeed_ = 0;
	int64_t lastGapNeed_ = 0;

	// pause (NET_PAUSE_SPEC.md)
	bool paused_ = false;
	int32_t pauseCycle_ = -1;  // the cycle the pause began after
	int pausedBy_ = -1;
	int32_t pauseMark_ = -1;   // cycle of the last pause command taken, -1 = none
	int32_t ended_ = -1;       // every pause that began at or before this cycle is over
	int endedBy_ = 0;
	int64_t lastPauseSent_ = 0;

	// chat (LAN_CHAT_NOTE.md)
	struct ChatOut {
		uint32_t number;
		uint16_t left;  // peers that have not answered yet
		int64_t sent;
	};
	std::vector<ChatOut> chatOut_;         // own lines not answered by everybody, oldest first
	uint32_t chatMade_ = 0;                // number of the last own line
	uint32_t chatTop_[kMaxPlayers] = {};   // highest number taken from each player, 0 = none
	uint32_t chatSeen_[kMaxPlayers] = {};  // bit i: the number chatTop_ - 1 - i was taken too

	// leaving
	bool lingering_ = false;
	int64_t lingerStart_ = 0;
	int64_t lastNeedSeen_ = 0;
	bool out_ = false;
};

} // namespace mesh
