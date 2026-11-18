#include "adventure_sync.h"
#include "net/protocol.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace adventure_sync;
static bool decode(const net::Writer& w) {
    net::Reader r(w.data.data(),w.size()); Packet p; return p.read(r);
}
int main() {
    Packet p; p.kind=Commit; p.mission={11,22,33}; p.run=7; p.visitor=9; p.sequence=1;
    p.goal={2,2,2,1161,UINT32_MAX,1};
    net::Writer valid; p.write(valid); assert(decode(valid));
    net::Reader r(valid.data.data(),valid.size()); Packet q;
    assert(q.read(r) && q.goal==p.goal && q.mission==p.mission && q.run==7 && q.visitor==9);
    for (size_t n=0;n<valid.size();++n) {
        net::Reader short_read(valid.data.data(),n); assert(!q.read(short_read));
    }
    auto reject=[&](Packet bad){net::Writer w; bad.write(w); assert(!decode(w));};
    q=p; q.sequence=0; reject(q);
    q=p; q.sequence=kMaxCredits+1; reject(q);
    q=p; q.run=0; reject(q);
    q=p; q.visitor=0; reject(q);
    q=p; q.goal.kind=0; reject(q);
    q=p; q.goal.kind=16; reject(q);
    q=p; q.goal.slot=8; reject(q);
    q=p; q.goal.act=16; reject(q);
    q=p; q.goal.required=0; reject(q);
    q=p; q.goal.required=kMaxRequired+1; reject(q);
    q=p; q.goal.count=1; reject(q); // count must stay below required
    // BUG-9089 (44): a counted defeat goal, a credit per object
    q=p; q.goal={0,0,3,1025,UINT32_MAX,5,0xABCD1234,4};
    { net::Writer w; q.write(w); net::Reader rr(w.data.data(),w.size()); Packet z; assert(z.read(rr) && z.goal==q.goal); }
    { Goal a={0,0,3,1025,UINT32_MAX,5,7,0}, b=a; b.count=3; assert(a.same_credit(b)); b.object=8; assert(!a.same_credit(b));
      Goal c={0,0,3,1025,UINT32_MAX,5,0,1}, d=c; assert(c.same_credit(d)); d.count=2; assert(!c.same_credit(d)); }
    // Death and Outcome
    { Packet d; d.kind=Death; d.mission={1,2,3}; d.run=5; d.visitor=6; d.object=0x1234; d.deaths=2;
      net::Writer w; d.write(w); assert(decode(w));
      Packet bad=d; bad.object=0; reject(bad); bad=d; bad.deaths=0; reject(bad); bad=d; bad.deaths=65; reject(bad); }
    { Packet o; o.kind=Outcome; o.mission={1,2,3}; o.run=5; o.visitor=6; o.state=6;
      net::Writer w; o.write(w); assert(decode(w)); Packet bad=o; bad.state=4; reject(bad); }
    // BUG-9114: a guest captain's hit on a host-driven creature
    { Packet h; h.kind=Hit; h.mission={1,2,3}; h.run=5; h.visitor=6; h.object=0x1234; h.key=0x99; h.damage=12.5f; h.damage_type=3;
      net::Writer w; h.write(w); assert(decode(w));
      net::Reader rr(w.data.data(),w.size()); Packet z; assert(z.read(rr) && z.key==0x99 && z.damage==12.5f && z.damage_type==3);
      for (size_t n=0;n<w.size();++n) { net::Reader s(w.data.data(),n); assert(!z.read(s)); }
      Packet bad=h; bad.key=0; reject(bad); bad=h; bad.object=0; reject(bad); bad=h; bad.damage=0; reject(bad);
      bad=h; bad.damage=-5; reject(bad); bad=h; bad.damage=2e6f; reject(bad); bad=h; bad.damage_type=256; reject(bad);
      bad=h; bad.damage=std::numeric_limits<float>::quiet_NaN(); reject(bad); }
    // BUG-9114: the host's creature states
    { ActorBatch b; b.mission={1,2,3}; b.run=5; b.time_ms=1000;
      for (uint32_t i=0;i<kMaxActorsPerBatch;++i) { Actor a; a.key=i+1; a.id=0x100+i; a.pos[0]=float(i); a.rot[3]=1; a.health=50; a.max_health=94; b.actors.push_back(a); }
      net::Writer w; b.write(w); assert(w.size()<=net::kMaxPacket);
      net::Reader rr(w.data.data(),w.size()); ActorBatch z; assert(z.read(rr) && z.actors.size()==kMaxActorsPerBatch && z.actors[7].pos[0]==7.f && z.actors[7].key==8);
      for (size_t n=0;n<w.size();++n) { net::Reader s(w.data.data(),n); ActorBatch t; assert(!t.read(s)); }
      auto bad_batch=[](ActorBatch x){ net::Writer bw; x.write(bw); net::Reader br(bw.data.data(),bw.size()); ActorBatch t; return !t.read(br); };
      ActorBatch x=b; x.run=0; assert(bad_batch(x));
      x=b; x.mission={}; assert(bad_batch(x));
      x=b; x.actors[0].key=0; assert(bad_batch(x));
      x=b; x.actors[0].id=0; assert(bad_batch(x));
      x=b; x.actors[0].pos[1]=std::numeric_limits<float>::infinity(); assert(bad_batch(x));
      x=b; x.actors[0].rot[0]=2.f; assert(bad_batch(x));
      x=b; x.actors[0].flags=2; assert(bad_batch(x));
      x=b; x.actors.push_back(b.actors[0]); assert(bad_batch(x)); // 21 > kMaxActorsPerBatch
      { net::Writer tw; b.write(tw); tw.u8(0); net::Reader tr(tw.data.data(),tw.size()); ActorBatch t; assert(!t.read(tr)); } // trailing byte
      ActorBatch empty=b; empty.actors.clear(); assert(!bad_batch(empty)); }
    q=p; q.mission={}; reject(q);
    q=p; q.kind=Kind(255); reject(q);
    valid.u8(1); assert(!decode(valid));
    p.kind=Hello; p.run=0; p.sequence=0;
    net::Writer hello; p.write(hello); assert(decode(hello));
    puts("PASS Adventure packets: round-trip, every truncation, bounds, unsupported goals, trailing bytes, counted credits, deaths, outcomes, hits, creature batches.");
}
