#include "ProbeSequence.hpp"
#include <cassert>
#include <vector>
#include <utility>
struct Mock {
    std::vector<std::pair<unsigned, uint32_t>> reads;
    unsigned failAt = 99, invalidAt = 99;
    bool readIdentity(unsigned cs, uint32_t reg, uint32_t& value) {
        reads.emplace_back(cs, reg);
        auto n = reads.size();
        if (n == failAt) return false;
        value = n == invalidAt ? 0xffffffffU : (reg ? 0xb2U : 0x35a40U);
        return true;
    }
};
int main() {
    using namespace g614jvr_probe;
    Mock ok; auto result = run(ok);
    assert(result.outcome == Outcome::Pass && result.completedReads == 4);
    assert((ok.reads == std::vector<std::pair<unsigned,uint32_t>>{{0,0},{0,4},{1,0},{1,4}}));
    for (unsigned i=1;i<=4;++i) {
        Mock failed; failed.failAt=i; auto f=run(failed);
        assert(f.outcome == Outcome::TransferError && failed.reads.size() == i);
        Mock invalid; invalid.invalidAt=i; auto v=run(invalid);
        assert(v.outcome == Outcome::InvalidResponse && invalid.reads.size() == i);
    }
}
