#include "KernelGPI0Observer/GPI0ReadOnly.hpp"
#include <cassert>
#include <map>
#include <vector>
using namespace gpi0_observer;
struct Fake final : Reader {
    std::map<uint32_t,uint32_t> words; std::vector<uint32_t> reads;
    bool read(uint32_t offset, uint32_t& value) override {
        reads.push_back(offset); auto it = words.find(offset);
        if (it == words.end()) return false;
        value = it->second; return true;
    }
};
int main() {
    Fake f; f.words = {{0,0x00940000},{4,1},{0xc,0x700},{0x840,1},{0x844,0},
                       {0xa8,0},{0x150,0},{0x110,0},{0x114,0}};
    Sample s{}; assert(sample(f,0x10000,s));
    assert(s.stride==16 && s.cfgOffset==0x840 && s.padOwnOffset==0xa8 && s.padOwnShift==16);
    assert((f.reads==std::vector<uint32_t>{0,4,0xc,0x840,0x844,0xa8,0x150,0x110,0x114}));
    f.reads.clear(); f.words[0]=0x01100000; f.words[0xf0]=0;
    assert(sample(f,0x10000,s) && s.padOwnOffset==0xf0);
    f.reads.clear(); f.words[0xc]=0xfff0;
    assert(!sample(f,0x10000,s) && f.reads.size()==3);
    f.reads.clear(); f.words[0xc]=0x100;
    assert(!sample(f,0x10000,s) && f.reads.size()==3);
    f.reads.clear(); assert(!sample(f,1,s) && f.reads.empty());
}
