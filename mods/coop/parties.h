// Decision 6J (owner 2026-11-18, docs/12-server-and-players.md#server-parties): parties of 4 inside one session (a
// galaxy planet holds up to 16 players: several parties). The host keeps the book; every player is in exactly one party
// (alone: a party of one).
//   * invite: a member of a party invites a player of another party (a right-click on its name / creature / tag); the
//     invited player accepts or declines; on accept it leaves its old party and joins the inviter's; the inviter's
//     party keeps its leader (an inviter alone leads the new party: "the person who invited")
//   * a player who joins a plain co-op session joins its host's party ("the person you joined"); on a galaxy planet a
//     player starts alone
//   * the leader hands the lead to another member; a leader who leaves: the longest standing member leads
//   * at most kMaxMembers; no invite across stages ("We can deny cross stage invites for now")
// Pure logic (tests/party_tests.cpp); the messages are in parties.cpp.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace parties {

constexpr size_t kMaxMembers = 4;
constexpr double kInviteSeconds = 60; // an unanswered invite lapses

enum class Result : uint8_t {
    Ok = 0,
    Unknown,    // no such player
    Self,       // oneself
    SameParty,  // already in the inviter's party
    Full,       // the inviter's party has kMaxMembers
    CrossStage, // the two play different stages
    NoInvite,   // no such invite (answered, lapsed, or never sent)
    NotLeader,  // only the leader hands the lead on
    NotMember,  // not in the leader's party
    Declined,   // the invited player said no
};

inline const char* text(Result r)
{
    switch (r) {
    case Result::Ok: return "ok";
    case Result::Unknown: return "that player is not here";
    case Result::Self: return "that is you";
    case Result::SameParty: return "already in your party";
    case Result::Full: return "your party is full (4 players)";
    case Result::CrossStage: return "that player plays another stage";
    case Result::NoInvite: return "the invite is gone";
    case Result::NotLeader: return "only the party's leader can do that";
    case Result::NotMember: return "that player is not in your party";
    case Result::Declined: return "declined";
    }
    return "?";
}

struct Party {
    uint16_t id = 0;
    uint8_t leader = 0;
    std::vector<uint8_t> members; // the longest standing first
    bool has(uint8_t p) const { return std::find(members.begin(), members.end(), p) != members.end(); }
};

class Book {
public:
    // a new player: in `with`'s party when it has room (a plain co-op join: the host's), else alone
    void add(uint8_t p, int with = -1)
    {
        if (find(p))
            return;
        if (with >= 0)
            if (Party* q = find((uint8_t)with); q && q->members.size() < kMaxMembers) {
                q->members.push_back(p);
                return;
            }
        alone(p);
    }
    // a player left the session: out of its party; a leader that leaves: the longest standing member leads
    void remove(uint8_t p)
    {
        drop_member(p);
        for (auto it = invites_.begin(); it != invites_.end();)
            it = it->first == p || it->second.from == p ? invites_.erase(it) : std::next(it);
    }
    // a player leaves its party (back to a party of its own)
    Result leave(uint8_t p)
    {
        Party* q = find(p);
        if (!q)
            return Result::Unknown;
        if (q->members.size() == 1)
            return Result::Ok;
        drop_member(p);
        alone(p);
        return Result::Ok;
    }
    // stage: the game mode each one plays (0: unknown, never refused for it)
    Result invite(uint8_t from, uint8_t to, uint32_t from_stage, uint32_t to_stage, double now)
    {
        Party* a = find(from);
        Party* b = find(to);
        if (!a || !b)
            return Result::Unknown;
        if (from == to)
            return Result::Self;
        if (a == b)
            return Result::SameParty;
        if (a->members.size() >= kMaxMembers)
            return Result::Full;
        if (from_stage && to_stage && from_stage != to_stage)
            return Result::CrossStage;
        invites_[to] = {from, now};
        return Result::Ok;
    }
    // the invited player answers: on yes it joins the inviter's party (checked again: it may be full by now)
    Result answer(uint8_t to, uint8_t from, bool yes, double now)
    {
        auto it = invites_.find(to);
        if (it == invites_.end() || it->second.from != from || now - it->second.at > kInviteSeconds)
            return Result::NoInvite;
        invites_.erase(it);
        if (!yes)
            return Result::Declined;
        Party* a = find(from);
        if (!a || !find(to))
            return Result::Unknown;
        if (a->has(to))
            return Result::SameParty;
        if (a->members.size() >= kMaxMembers)
            return Result::Full;
        drop_member(to);
        a = find(from); // (drop_member may have erased a party: find again)
        a->members.push_back(to);
        return Result::Ok;
    }
    Result set_leader(uint8_t by, uint8_t to)
    {
        Party* q = find(by);
        if (!q)
            return Result::Unknown;
        if (q->leader != by)
            return Result::NotLeader;
        if (!q->has(to))
            return Result::NotMember;
        q->leader = to;
        return Result::Ok;
    }
    void lapse(double now)
    {
        for (auto it = invites_.begin(); it != invites_.end();)
            it = now - it->second.at > kInviteSeconds ? invites_.erase(it) : std::next(it);
    }
    int pending_from(uint8_t to) const
    {
        auto it = invites_.find(to);
        return it == invites_.end() ? -1 : it->second.from;
    }
    Party* find(uint8_t p)
    {
        for (auto& q : parties_)
            if (q.has(p))
                return &q;
        return nullptr;
    }
    const Party* find(uint8_t p) const { return const_cast<Book*>(this)->find(p); }
    const std::vector<Party>& all() const { return parties_; }
    void clear()
    {
        parties_.clear();
        invites_.clear();
    }
    // a guest takes the host's table as it is (parties.cpp)
    void set_all(std::vector<Party> v) { parties_ = std::move(v); }

private:
    struct Pending {
        uint8_t from = 0;
        double at = 0;
    };
    std::vector<Party> parties_;
    std::map<uint8_t, Pending> invites_; // invited player -> its invite (one at a time: the newest)
    uint16_t next_id_ = 1;

    void alone(uint8_t p)
    {
        Party q;
        q.id = next_id_++;
        if (!next_id_)
            next_id_ = 1;
        q.leader = p;
        q.members.push_back(p);
        parties_.push_back(q);
    }
    void drop_member(uint8_t p)
    {
        for (auto it = parties_.begin(); it != parties_.end(); ++it) {
            auto m = std::find(it->members.begin(), it->members.end(), p);
            if (m == it->members.end())
                continue;
            it->members.erase(m);
            if (it->members.empty())
                parties_.erase(it);
            else if (it->leader == p)
                it->leader = it->members.front(); // the longest standing member leads
            return;
        }
    }
};

} // namespace parties
