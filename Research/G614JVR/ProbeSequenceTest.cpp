#include "ProbeSequence.hpp"
#include "IdentityFrame.hpp"
#include "IdentityReadTransport.hpp"
#include <cassert>
#include <vector>
#include <utility>
struct Mock {
    std::vector<std::pair<unsigned, uint32_t>> reads;
    unsigned failAt = 99, invalidAt = 99;
    uint32_t invalidValue = 0xffffffffU;
    bool inactive = true;
    bool externalSelectProvenInactive() const { return inactive; }
    bool readIdentity(unsigned cs, uint32_t reg, uint32_t& value) {
        reads.emplace_back(cs, reg);
        auto n = reads.size();
        if (n == failAt) return false;
        value = n == invalidAt ? invalidValue : (reg ? 0xb2U : 0x35a40U);
        return true;
    }
};
struct FakeWire {
    unsigned calls = 0;
    bool inactive = true, fail = false;
    bool prepareAndVerifyExternalSelect() { return inactive; }
    bool exchangeRead(unsigned cs, const uint8_t* tx, uint8_t* rx, size_t n) {
        assert(n == 10 && tx[0] == 0x80 && cs == calls / 2);
        assert(tx[3] == (calls % 2 ? 4 : 0));
        for (unsigned i : {1U,2U,4U,5U,6U,7U,8U,9U}) assert(tx[i] == 0);
        ++calls;
        if (fail) return false;
        rx[6]=0; rx[7]=3; rx[8]=0x5a; rx[9]=0x40;
        return true;
    }
};
int main() {
    using namespace g614jvr_probe;
    IdentityFrame frame{};
    assert(makeIdentityReadFrame(0, frame));
    assert(frame.bytes[0] == 0x80 && frame.bytes[3] == 0);
    assert(makeIdentityReadFrame(4, frame));
    assert(frame.bytes[0] == 0x80 && frame.bytes[3] == 4);
    assert(!makeIdentityReadFrame(8, frame));
    uint8_t rx[10]{0,0,0,0,0,0,0,3,0x5a,0x40};
    assert(decodeIdentityResponse(rx,10) == 0x35a40);
    assert(decodeIdentityResponse(rx,9) == 0);
    FakeWire wire; IdentityReadTransport<FakeWire> adapter(wire);
    auto wireResult=run(adapter);
    assert(wireResult.outcome==Outcome::Pass && wire.calls==4);
    uint32_t ignored=0;
    assert(!adapter.readIdentity(0,0,ignored) && wire.calls==4);
    FakeWire broken; broken.fail=true; IdentityReadTransport<FakeWire> failedAdapter(broken);
    assert(run(failedAdapter).outcome==Outcome::TransferError && broken.calls==1);
    assert(!failedAdapter.readIdentity(0,0,ignored) && broken.calls==1);
    Mock ok; auto result = run(ok);
    assert(result.outcome == Outcome::Pass && result.completedReads == 4);
    assert((ok.reads == std::vector<std::pair<unsigned,uint32_t>>{{0,0},{0,4},{1,0},{1,4}}));
    Mock unsafe; unsafe.inactive = false;
    assert(run(unsafe).outcome == Outcome::UnsafeSelectState && unsafe.reads.empty());
    for (unsigned i=1;i<=4;++i) {
        Mock failed; failed.failAt=i; auto f=run(failed);
        assert(f.outcome == Outcome::TransferError && failed.reads.size() == i);
        Mock invalid; invalid.invalidAt=i; auto v=run(invalid);
        assert(v.outcome == Outcome::InvalidResponse && invalid.reads.size() == i);
        Mock zero; zero.invalidAt=i; zero.invalidValue=0; auto z=run(zero);
        assert(z.outcome == Outcome::InvalidResponse && zero.reads.size() == i);
    }
}
