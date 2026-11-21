// Disassembles a range of a flat XEX image dump.
// Usage: ppcdis <default.bin> <start_addr> <count> [base=0x82000000]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <disasm.h>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: ppcdis <image.bin> <addr> <count> [base]\n");
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(in)), {});
    uint64_t addr = strtoull(argv[2], nullptr, 0);
    int count = atoi(argv[3]);
    uint64_t base = argc > 4 ? strtoull(argv[4], nullptr, 0) : 0x82000000;

    for (int i = 0; i < count; i++, addr += 4)
    {
        size_t off = addr - base;
        if (off + 4 > img.size())
            break;
        const uint8_t* p = img.data() + off;
        uint32_t word = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        ppc_insn insn{};
        ppc::Disassemble(p, addr, insn);
        if (insn.opcode)
            printf("%08llX  %08X  %-10s %s\n", (unsigned long long)addr, word, insn.opcode->name, insn.op_str);
        else
            printf("%08llX  %08X  .long\n", (unsigned long long)addr, word);
    }
    return 0;
}
