// net_mesh — see net_mesh.h. Paragraph numbers refer to LAN_MESH_SPEC.md.
#include "net_mesh.h"

#include <algorithm>

namespace mesh {

static inline bool Bit(uint16_t mask, int p) { return (mask >> p) & 1; }

Mesh::Mesh(int players, int me, const Params &params, int64_t now) : n_(players), me_(me), P(params)
{
	for (int p = 0; p < kMaxPlayers; ++p) {
		// A.2 step 7: cycles 0..kLag of every player are pre-filled with empty records.
		cmd_[p].assign(kLag + 1, 0);
		st_[p].assign(kLag + 1, 1);
		have_[p] = kLag;
		removedAt_[p] = kNever;
		reason_[p] = Reason::Lost;
		commitMask_[p] = 0xFFFF;
		lastHeard_[p] = lastAdvance_[p] = now;
	}
	loadedFrom_ = uint16_t(1u << me_);
}

uint16_t Mesh::AliveMask() const
{
	uint16_t m = 0;
	for (int p = 0; p < n_; ++p) {
		if (p == me_ || Alive(p)) m |= uint16_t(1u << p);
	}
	return m;
}

uint16_t Mesh::RemovedMask() const
{
	uint16_t m = 0;
	for (int p = 0; p < n_; ++p) {
		if (p != me_ && removedAt_[p] != kNever) m |= uint16_t(1u << p);
	}
	return m;
}

uint16_t Mesh::LiveMaskAt(int32_t cycle) const
{
	uint16_t m = 0;
	for (int p = 0; p < n_; ++p) {
		if (removedAt_[p] > cycle) m |= uint16_t(1u << p);
	}
	return m;
}

void Mesh::SendPeers(const Msg &m, bool alsoSuspects)
{
	for (int p = 0; p < n_; ++p) {
		if (p == me_ || removedAt_[p] != kNever) continue;
		if (suspected_[p] && !alsoSuspects) continue;
		Send(p, m);
	}
}

void Mesh::SendCommit(int to, int p)
{
	Msg m;
	m.kind = Kind::Commit;
	m.player = uint8_t(p);
	m.cycle = removedAt_[p];
	m.flags = uint8_t(reason_[p]);
	m.mask = commitMask_[p];
	m.round = round_[p];
	Send(to, m);
}

void Mesh::SelfOut(Reason why)
{
	if (out_) return;
	out_ = true;
	events.push_back({Event::SelfOut, me_, executed_, why, 0});
}

// B.3 first write wins; a packet at or beyond a removal cycle is dropped. Returns true if stored.
bool Mesh::Put(int p, int32_t cycle, uint32_t data, bool quit, int64_t now)
{
	if (cycle < 0 || cycle >= removedAt_[p] || cycle > have_[p] + 4096) return false;
	if ((int32_t)st_[p].size() <= cycle) {
		st_[p].resize(cycle + 1, 0);
		cmd_[p].resize(cycle + 1, 0);
	}
	if (st_[p][cycle] != 0) return false;
	st_[p][cycle] = quit ? 2 : 1;
	cmd_[p][cycle] = data;
	// A packet beyond what p could make before the pause: p has run the next cycle, so the pause
	// is over (the message that said so was lost). A quit is made without running a cycle.
	if (paused_ && p != me_ && !quit && cycle > pauseCycle_ + kLag + 1) EndPause(pauseCycle_, p);
	while (Hold(p, have_[p] + 1)) {
		++have_[p];
		lastAdvance_[p] = now; // B.3: the suspicion clock restarts whenever `have` advances
		if (st_[p][have_[p]] == 2) {
			// B.6 step 3: the quit is in the stream; the player is gone from that cycle on.
			if (p != me_) Remove(p, have_[p], Reason::Left, 0xFFFF, now);
			break;
		}
	}
	return true;
}

// B.5 rule 6 (apply) and B.6: player p takes no part from cycle C on.
void Mesh::Remove(int p, int32_t C, Reason why, uint16_t mask, int64_t now)
{
	if (removedAt_[p] != kNever) return;
	if (executed_ >= C) {
		// Fact F1 says this cannot happen. If it does, this device ran p's commands of a cycle
		// the others run without them: it must not go on as if nothing happened.
		SelfOut(Reason::Desync);
		return;
	}
	const bool holdsQuit = Hold(p, C) && st_[p][C] == 2;
	const int32_t keep = holdsQuit ? C + 1 : C;
	if ((int32_t)st_[p].size() > keep) {
		st_[p].resize(keep);
		cmd_[p].resize(keep);
	}
	have_[p] = std::min(have_[p], keep - 1);
	removedAt_[p] = C;
	reason_[p] = why;
	commitMask_[p] = mask;
	suspected_[p] = false;
	events.push_back({Event::PlayerRemoved, p, C, why, 0});
	// Changed by the model: a commit names its alive set; every player outside it is doomed —
	// suspected now and never taken back — so that nobody rejoins a group that already decided
	// without him.
	for (int q = 0; q < n_; ++q) {
		if (q == me_ || Bit(mask, q) || removedAt_[q] != kNever) continue;
		doomed_[q] = true;
		if (!suspected_[q]) Suspect(q, now);
	}
}

// B.5 rules 1-2: from here no packet from p's address is accepted (the test is in OnMessage).
void Mesh::Suspect(int p, int64_t now)
{
	suspected_[p] = true;
	suspectSince_[p] = now;
	lastStatus_ = now - P.statusMs; // announce at the next tick
}

// B.5 rule 7: the round is void; p is heard again; a new suspicion needs hearMs of grace.
void Mesh::Unfreeze(int p, uint16_t round, int64_t now)
{
	suspected_[p] = false;
	round_[p] = round;
	graceUntil_[p] = now + P.hearMs;
}

// B.5 rule 1 / B.4 steps 5-6 / B.8: has p been silent beyond this device's own bound?
// `left` receives the time to the bound, or -1 while the silence is too short to be shown.
bool Mesh::BoundExpired(int p, int64_t now, int64_t *left) const
{
	*left = -1;
	if (!loaded_) return false; // the own clocks start at the own end of load (B.4 step 5)
	if (!Bit(loadedFrom_, p)) {
		const int64_t quiet = now - std::max(lastHeard_[p], loadedAt_);
		*left = quiet < P.plateMs ? P.loadMs - (now - loadedAt_)
		                          : std::min(P.loadPingDropMs - quiet, P.loadMs - (now - loadedAt_));
		return quiet >= P.loadPingDropMs || now - loadedAt_ >= P.loadMs;
	}
	const int64_t bound = DropFor(p);
	const int64_t since = std::max(lastHeard_[p], lastAdvance_[p]);
	if (now - lastHeard_[p] >= P.plateMs) *left = bound - (now - since);
	return now - since >= bound;
}

// B.5 rule 3: "hears" p. A device whose own bound for p has run out never vouches for p.
bool Mesh::HeardRecently(int p, int64_t now) const
{
	int64_t left;
	if (BoundExpired(p, now, &left)) return false;
	if (now < graceUntil_[p]) return true;
	return now - lastHeard_[p] < P.hearMs;
}

// B.8: 60 s per absence, the rest of the allowance if that is smaller, "at once" when it is used.
int64_t Mesh::DropFor(int p) const
{
	const int64_t rest = P.allowanceMs - used_[p];
	if (rest <= 0) return P.exhaustedDropMs;
	return std::max(P.exhaustedDropMs, std::min(P.dropMs, rest));
}

// B.5 rule 5: "the same round, the same alive mask" — taken over the whole view.
bool Mesh::ViewEqual(const Msg &s, uint16_t alive) const
{
	if (s.mask != alive || s.removed != RemovedMask()) return false;
	for (int q = 0; q < n_; ++q) {
		if (!Bit(alive, q) && removedAt_[q] == kNever && s.rounds[q] != round_[q]) return false;
	}
	return true;
}

Msg Mesh::MakeStatus()
{
	Msg m;
	m.kind = Kind::Status;
	m.player = uint8_t(me_);
	m.data = ++seq_;
	m.mask = AliveMask();
	m.removed = RemovedMask();
	m.want = want_;
	for (int p = 0; p < n_; ++p) {
		m.rounds[p] = round_[p];
		m.have[p] = have_[p];
	}
	return m;
}

// B.3 Need: answer to the asker only — the own packets, and with the relay flag any held packet
// of the named players. A short run of cycles is sent so that one request heals a burst.
void Mesh::AnswerNeed(int from, const Msg &m)
{
	for (int q = 0; q < n_; ++q) {
		if (!Bit(m.mask, q) || q == from) continue;
		if (q != me_ && !(m.flags & 1)) continue;
		if (q != me_ && removedAt_[q] != kNever && removedAt_[q] <= m.cycle && !lingering_) {
			SendCommit(from, q); // the asker waits for a packet that will never exist
		}
		for (int32_t c = m.cycle; c <= m.cycle + kLag; ++c) {
			if (!Hold(q, c)) break;
			Msg a;
			a.kind = Kind::Cmd;
			a.player = uint8_t(q);
			a.cycle = c;
			a.data = cmd_[q][c];
			a.flags = st_[q][c] == 2 ? 1 : 0;
			Send(from, a);
		}
	}
}

// The pause that began after `cycle`, and every earlier one, is over; `by` let the game go on.
void Mesh::EndPause(int32_t cycle, int by)
{
	if (cycle <= ended_) return;
	ended_ = cycle;
	endedBy_ = by;
	if (paused_ && pauseCycle_ <= ended_) {
		paused_ = false;
		events.push_back({Event::Resumed, by, pauseCycle_, Reason::Lost, 0});
	}
}

// A chat line of the local player. Sent directly to every peer; Tick repeats it.
uint32_t Mesh::Chat(int64_t now)
{
	if (out_ || lingering_) return 0;
	if (!chatOut_.empty() && chatMade_ + 1 - chatOut_.front().number >= kChatWindow) return 0;
	ChatOut c{++chatMade_, 0, now};
	Msg m;
	m.kind = Kind::Chat;
	m.player = uint8_t(me_);
	m.data = c.number;
	for (int p = 0; p < n_; ++p) {
		if (p == me_ || removedAt_[p] != kNever) continue;
		c.left |= uint16_t(1u << p);
		if (!suspected_[p]) Send(p, m); // a frozen peer gets it when it is taken back
	}
	if (c.left) chatOut_.push_back(c);
	return c.number;
}

// A chat line of `from` has arrived. True if it is a new one (first arrival).
bool Mesh::TakeChat(int from, uint32_t number)
{
	uint32_t &top = chatTop_[from], &seen = chatSeen_[from];
	if (number > top) {
		const uint32_t shift = number - top;
		seen = shift >= 32 ? 0 : seen << shift;
		if (top != 0 && shift <= 32) seen |= 1u << (shift - 1);
		top = number;
		return true;
	}
	if (number == top) return false;
	const uint32_t back = top - number - 1;
	if (back >= 32 || (seen >> back) & 1) return false; // kChatWindow: nothing new can be that old
	seen |= 1u << back;
	return true;
}

bool Mesh::Pause(int by, int64_t now)
{
	if (out_ || lingering_ || !released_ || by < 0 || by >= n_) return false;
	// Pressed before its sender could know of the pause that had just begun.
	if (pauseMark_ >= 0 && executed_ <= pauseMark_ + kLag + 1) return false;
	pauseMark_ = executed_;
	if (executed_ <= ended_) return true; // the others have gone on already: this device was behind
	paused_ = true;
	pauseCycle_ = executed_;
	pausedBy_ = by;
	lastPauseSent_ = now - P.statusMs; // tell at the next tick
	events.push_back({Event::Paused, by, pauseCycle_, Reason::Lost, 0});
	return true;
}

void Mesh::Resume(int64_t /*now*/)
{
	if (!paused_ || out_ || lingering_) return;
	Msg m;
	m.kind = Kind::Pause;
	m.player = uint8_t(me_);
	m.cycle = pauseCycle_;
	SendPeers(m, true);
	EndPause(pauseCycle_, me_);
}

void Mesh::OnLoaded(int64_t now)
{
	loaded_ = true;
	loadedAt_ = now;
	lastLoadedSent_ = now - P.loadedMs;
}

bool Mesh::CanExecute(int32_t cycle) const
{
	if (out_ || lingering_ || !released_ || paused_ || cycle != executed_ + 1) return false;
	for (int p = 0; p < n_; ++p) {
		if (removedAt_[p] > cycle && have_[p] < cycle) return false; // F1
	}
	return true;
}

void Mesh::OnExecuted(int32_t cycle)
{
	executed_ = cycle;
	// A.3 step 1: the own packet for cycle + lag + 1, to every peer still in the game (B.3).
	const int32_t c = nextOwn_++;
	cmd_[me_].resize(c + 1, 0);
	st_[me_].resize(c + 1, 0);
	cmd_[me_][c] = pending_;
	st_[me_][c] = 1;
	have_[me_] = c;
	Msg m;
	m.kind = Kind::Cmd;
	m.player = uint8_t(me_);
	m.cycle = c;
	m.data = pending_;
	pending_ = 0;
	SendPeers(m, true);
}

// B.6 steps 1-2: the quit goes into the own slot of the next free cycle, then the device lingers.
void Mesh::Quit(int64_t now)
{
	if (out_ || lingering_) return;
	const int32_t c = nextOwn_++;
	cmd_[me_].resize(c + 1, 0);
	st_[me_].resize(c + 1, 0);
	st_[me_][c] = 2;
	have_[me_] = c;
	Msg m;
	m.kind = Kind::Cmd;
	m.player = uint8_t(me_);
	m.cycle = c;
	m.flags = 1;
	SendPeers(m, true);
	removedAt_[me_] = c;
	reason_[me_] = Reason::Left;
	lingering_ = true;
	lingerStart_ = lastNeedSeen_ = now;
	events.push_back({Event::SelfOut, me_, c, Reason::Left, 0});
}

void Mesh::OnMessage(int from, const Msg &m, int64_t now)
{
	if (from < 0 || from >= n_ || from == me_ || out_) return;
	if (lingering_) {
		if (m.kind == Kind::Need) { // B.6 step 2
			lastNeedSeen_ = now;
			AnswerNeed(from, m);
		}
		return;
	}
	if (removedAt_[from] != kNever) {
		if (reason_[from] == Reason::Left) {
			// B.6 step 3: a lingering leaver still supplies its own earlier packets.
			if (m.kind == Kind::Cmd && m.player == from) Put(from, m.cycle, m.data, m.flags & 1, now);
		} else {
			// B.5 rule 10: a removed player gets the commit as the answer — but never as an
			// answer to a commit, and at most once per status interval (after a healed split both
			// sides have removed each other and would answer each other for ever).
			if (m.kind != Kind::Commit && now - lastTold_[from] >= P.statusMs) {
				lastTold_[from] = now;
				SendCommit(from, from);
			}
		}
		return;
	}
	if (suspected_[from]) return; // B.5 rule 2: frozen

	// B.3 liveness = any valid packet from the player's own address. B.8: an absence longer than
	// the plate threshold is charged to the player's allowance when he is heard again.
	const int64_t gap = now - lastHeard_[from];
	if (released_ && Bit(loadedFrom_, from) && gap >= P.plateMs) used_[from] += gap;
	lastHeard_[from] = now;

	switch (m.kind) {
	case Kind::Cmd: {
		const int p = m.player;
		if (p >= n_ || p == me_) break;
		if (p == from) loadedFrom_ |= uint16_t(1u << from); // it runs, so it has loaded
		// A relay (p != from) is accepted from any unsuspected peer, also for a suspect (rule 2).
		if (Put(p, m.cycle, m.data, m.flags & 1, now)) viaRelay_[p] = (p != from);
		break;
	}
	case Kind::Loading:
		break;
	case Kind::Loaded:
		// Changed by the model: the `seen` mask is taken as second-hand knowledge, so that two
		// devices that cannot reach each other directly still learn that the other has loaded.
		loadedFrom_ |= uint16_t((m.mask & ((1u << n_) - 1)) | (1u << from));
		if (loaded_ && (loadedFrom_ & ~m.mask) != 0) { // B.4 step 3: the peer lacks this device's, or one this device knows of
			Msg a;
			a.kind = Kind::Loaded;
			a.mask = loadedFrom_;
			Send(from, a);
		}
		break;
	case Kind::Need:
		AnswerNeed(from, m);
		break;
	case Kind::Status: {
		const int o = m.player; // whose view; o != from is a view relayed on request
		if (o >= n_ || o == me_) break;
		if (snapValid_[o] && m.data <= snap_[o].data) break; // older or duplicate
		snap_[o] = m;
		snapValid_[o] = true;
		snapAt_[o] = now;
		if (!Alive(o)) break;
		bool reply = Bit(m.want, me_);
		for (int q = 0; q < n_; ++q) {
			if (q == me_ || q == o) continue;
			const bool theyRemoved = Bit(m.removed, q);
			if (removedAt_[q] != kNever) {
				if (!theyRemoved) SendCommit(o, q); // rule 6: who applied a commit re-announces it
				continue;
			}
			if (theyRemoved) { // they know a removal this device does not: show them this device's view
				reply = true;
				continue;
			}
			if (m.rounds[q] > round_[q]) { // rule 7 / 8(c): a cancel this device missed
				if (suspected_[q] && !doomed_[q]) {
					Unfreeze(q, m.rounds[q], now);
				} else {
					round_[q] = m.rounds[q];
				}
			}
			if (!Bit(m.mask, q)) { // o suspects q
				reply = true; // rule 3: answer with the own view (q alive in it = "hears")
				if (!suspected_[q]) {
					if (!HeardRecently(q, now)) {
						Suspect(q, now); // rule 3: adopt
					} else if (!doomed_[q] && m.rounds[q] >= round_[q]) {
						// rule 7, made explicit by the model: a decider that hears q itself cancels
						// the round of the device that does not. "Decider" is taken in the own view
						// or in the view of the suspecting device (its lowest alive player), so
						// that a device that has wrongly given up the real decider is still reached.
						int decider = 0, theirs = 0;
						const uint16_t G = AliveMask();
						while (!Bit(G, decider)) ++decider;
						while (theirs < n_ && !Bit(m.mask, theirs)) ++theirs;
						if (theirs == me_) decider = me_;
						if (decider == me_) {
							Msg c;
							c.kind = Kind::Cancel;
							c.player = uint8_t(q);
							c.round = m.rounds[q];
							SendPeers(c, false);
							round_[q] = uint16_t(m.rounds[q] + 1);
						}
					}
				}
			}
		}
		if (reply) replyMask_ |= uint16_t(1u << o);
		// Changed by the model: views are relayed on request, so that a decider that cannot
		// reach one peer directly still gets that peer's view.
		for (int i = 0; i < n_; ++i) {
			if (Bit(m.want, i) && i != me_ && i != o && snapValid_[i]) Send(o, snap_[i]);
		}
		break;
	}
	case Kind::Commit: {
		const int p = m.player;
		if (p >= n_) break;
		if (p == me_) { // rule 10, B.4 step 7
			SelfOut(Reason::ConnectionLost);
			break;
		}
		if (removedAt_[p] != kNever) {
			if (removedAt_[p] != m.cycle) ++conflicts;
			break;
		}
		if (!Bit(m.mask, me_)) break; // the sender's group has dropped this device; its own commit follows
		Remove(p, m.cycle, Reason(m.flags), m.mask, now); // rule 6
		break;
	}
	case Kind::Pause:
		if (m.flags & 1) {
			// The sender stands after a cycle whose pause this device knows to be over.
			if (m.cycle <= ended_) {
				Msg a;
				a.kind = Kind::Pause;
				a.player = uint8_t(endedBy_);
				a.cycle = m.cycle;
				Send(from, a);
			}
		} else if (m.player < n_ && m.cycle <= executed_ + kLag + 1) {
			// No device can be further ahead than that; a pause this device has not reached yet
			// is not begun when it gets there.
			EndPause(m.cycle, m.player);
		}
		break;
	case Kind::Chat: {
		// Only from the player himself. Every arrival is answered: the answer to the first one
		// may have been lost.
		if (m.player != from || m.data == 0) break;
		Msg a;
		a.kind = Kind::ChatAck;
		a.data = m.data;
		Send(from, a);
		if (TakeChat(from, m.data)) events.push_back({Event::ChatLine, from, int32_t(m.data), Reason::Lost, 0});
		break;
	}
	case Kind::ChatAck:
		for (size_t i = 0; i < chatOut_.size(); ++i) {
			if (chatOut_[i].number != m.data) continue;
			chatOut_[i].left &= uint16_t(~(1u << from));
			if (!chatOut_[i].left) chatOut_.erase(chatOut_.begin() + i);
			break;
		}
		break;
	case Kind::Cancel: {
		const int p = m.player;
		if (p >= n_ || p == me_ || removedAt_[p] != kNever) break;
		if (suspected_[p] && m.round == round_[p] && !doomed_[p]) {
			Unfreeze(p, uint16_t(m.round + 1), now); // rule 7
		} else if (!suspected_[p] && round_[p] <= m.round) {
			round_[p] = uint16_t(m.round + 1);
		}
		break;
	}
	}
}

void Mesh::Tick(int64_t now)
{
	if (out_) return;
	if (lingering_) { // B.6 step 2
		if (now - lastNeedSeen_ >= P.lingerQuietMs || now - lingerStart_ >= P.lingerCapMs) out_ = true;
		return;
	}

	// --- NET_PAUSE_SPEC.md: a standing device says so; that is its sign of life ---
	if (paused_ && now - lastPauseSent_ >= P.statusMs) {
		lastPauseSent_ = now;
		Msg m;
		m.kind = Kind::Pause;
		m.player = uint8_t(pausedBy_);
		m.flags = 1;
		m.cycle = pauseCycle_;
		SendPeers(m, true);
	}

	// --- chat: a line is repeated to who has not answered; a removed peer is not waited for ---
	for (size_t i = 0; i < chatOut_.size();) {
		ChatOut &c = chatOut_[i];
		const bool again = now - c.sent >= P.chatMs;
		Msg m;
		m.kind = Kind::Chat;
		m.player = uint8_t(me_);
		m.data = c.number;
		for (int p = 0; p < n_; ++p) {
			if (!Bit(c.left, p)) continue;
			if (removedAt_[p] != kNever) {
				c.left &= uint16_t(~(1u << p));
			} else if (again && !suspected_[p]) {
				Send(p, m);
			}
		}
		if (again) c.sent = now;
		if (!c.left) {
			chatOut_.erase(chatOut_.begin() + i);
		} else {
			++i;
		}
	}

	// --- B.4 start barrier ---
	if (!released_) {
		if (now - lastLoadedSent_ >= P.loadedMs) { // steps 1 and 6
			lastLoadedSent_ = now;
			Msg m;
			m.kind = loaded_ ? Kind::Loaded : Kind::Loading;
			m.mask = loadedFrom_;
			SendPeers(m, true);
		}
		if (loaded_) { // step 2: release when every peer still in the game has reported
			bool all = true;
			for (int p = 0; p < n_; ++p) {
				if (p != me_ && removedAt_[p] == kNever && !Bit(loadedFrom_, p)) all = false;
			}
			if (all) {
				released_ = true;
				events.push_back({Event::Started, me_, 0, Reason::Lost, 0});
			}
		}
	}

	// --- B.5 rule 1 / B.4 steps 5-6 / B.8: who is silent beyond the bound ---
	bool open = false;
	for (int p = 0; p < n_; ++p) open = open || suspected_[p];
	if (loaded_) {
		for (int p = 0; p < n_; ++p) {
			if (p == me_ || !Alive(p) || now < graceUntil_[p]) continue;
			int64_t left;
			bool sus = BoundExpired(p, now, &left);
			// Rule 8, widened by the model to every peer: while a round is open everybody talks
			// four times a second, so hearMs of silence is plain.
			if (open && Bit(loadedFrom_, p) && now - lastHeard_[p] >= P.hearMs) sus = true;
			if (sus) {
				Suspect(p, now);
				open = true;
			} else if (left >= 0 && now - lastWait_[p] >= 1000) {
				lastWait_[p] = now;
				events.push_back({Event::Waiting, p, 0, Reason::Lost, int32_t(left)});
			}
		}
	}

	if (open) {
		// --- B.5 rule 11: hearing nobody in a game of three or more = this device is the lost one
		int others = 0, silent = 0;
		for (int p = 0; p < n_; ++p) {
			if (p == me_ || removedAt_[p] != kNever) continue;
			++others;
			if (suspected_[p]) ++silent;
		}
		if (others >= 2 && silent == others) {
			SelfOut(Reason::Isolated);
			return;
		}

		// --- B.5 rule 5: the decider = the lowest player of the own alive set ---
		const uint16_t G = AliveMask();
		int decider = 0;
		while (!Bit(G, decider)) ++decider;
		want_ = 0;
		if (decider == me_) {
			for (int p = 0; p < n_; ++p) {
				if (!suspected_[p]) continue;
				bool cancel = false, agree = true;
				for (int i = 0; i < n_; ++i) {
					if (i == me_ || !Bit(G, i)) continue;
					if (!snapValid_[i]) {
						agree = false;
						want_ |= uint16_t(1u << i);
						continue;
					}
					const Msg &s = snap_[i];
					// rule 7: somebody of the alive set still hears p (a view from after the
					// suspicion became known, not one that was already on its way)
					if (!doomed_[p] && Bit(s.mask, p) && snapAt_[i] >= suspectSince_[p] + 2 * P.statusMs) {
						cancel = true;
					}
					if (snapAt_[i] < suspectSince_[p] || !ViewEqual(s, G)) {
						agree = false;
						want_ |= uint16_t(1u << i);
					} else if (s.have[p] != have_[p]) {
						agree = false; // rule 4: the fill is not finished
					}
				}
				if (cancel) {
					Msg m;
					m.kind = Kind::Cancel;
					m.player = uint8_t(p);
					m.round = round_[p];
					SendPeers(m, false);
					Unfreeze(p, uint16_t(round_[p] + 1), now);
				} else if (agree) {
					// rule 5: all views equal, all frozen, equal `have`  =>  C = have + 1
					Remove(p, have_[p] + 1, Reason::Lost, G, now);
					if (out_) return;
					for (int q = 0; q < n_; ++q) {
						if (q != me_ && (q == p || removedAt_[q] == kNever)) SendCommit(q, p);
					}
				}
			}
		}

		// --- B.5 rule 2: announce the view; rule 4: fill the suspects' packets by relay ---
		if (now - lastStatus_ >= P.statusMs) {
			lastStatus_ = now;
			replyMask_ = 0;
			SendPeers(MakeStatus(), true);
			for (int p = 0; p < n_; ++p) {
				if (!suspected_[p]) continue;
				Msg m;
				m.kind = Kind::Need;
				m.cycle = have_[p] + 1;
				m.mask = uint16_t(1u << p);
				m.flags = 1;
				SendPeers(m, false);
			}
		}
	}
	if (replyMask_) { // rule 3: reply duty
		const Msg s = MakeStatus();
		for (int p = 0; p < n_; ++p) {
			if (Bit(replyMask_, p) && removedAt_[p] == kNever) Send(p, s);
		}
		replyMask_ = 0;
	}

	// --- B.3 Need: ask for what the next cycle lacks ---
	if (released_) {
		const int32_t c = executed_ + 1;
		uint16_t who = 0;
		bool relay = false;
		for (int p = 0; p < n_; ++p) {
			if (removedAt_[p] > c && have_[p] < c) {
				who |= uint16_t(1u << p);
				relay = relay || viaRelay_[p]; // the direct path failed last time: ask all at once
			}
		}
		// Changed by the model: a gap that is already visible (a later packet of p is held, an
		// earlier one is not) is asked for at once, before the game reaches it.
		if (now - lastGapNeed_ >= P.needMs) {
			lastGapNeed_ = now;
			for (int p = 0; p < n_; ++p) {
				if (p == me_ || removedAt_[p] != kNever || Bit(who, p)) continue;
				if ((int32_t)st_[p].size() - 1 <= have_[p]) continue;
				Msg m;
				m.kind = Kind::Need;
				m.cycle = have_[p] + 1;
				m.mask = uint16_t(1u << p);
				m.flags = viaRelay_[p] ? 1 : 0;
				if (viaRelay_[p]) {
					SendPeers(m, false);
				} else if (!suspected_[p]) {
					Send(p, m);
				}
			}
		}
		if (!who) {
			stallCycle_ = -1;
		} else {
			if (stallCycle_ != c) {
				stallCycle_ = c;
				stallSince_ = now;
				lastNeed_ = now - P.needMs;
			}
			if (now - lastNeed_ >= P.needMs) {
				lastNeed_ = now;
				Msg m;
				m.kind = Kind::Need;
				m.cycle = c;
				m.mask = who;
				m.flags = (relay || now - stallSince_ >= P.relayAfterMs) ? 1 : 0;
				SendPeers(m, false);
			}
		}
	}
}

} // namespace mesh
