// Dumps a decrypted/decompressed XEX image to a flat file using XenonUtils,
// and prints its sections and import symbols.
// Usage: xexdump <default.xex> <out.bin> <out_symbols.txt>
#include <cstdio>
#include <fstream>
#include <vector>
#include <image.h>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: xexdump <in.xex> <out.bin> <out_symbols.txt>\n");
        return 1;
    }

    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
    Image image = Image::ParseImage(file.data(), file.size());
    if (!image.data)
    {
        fprintf(stderr, "failed to parse image\n");
        return 1;
    }

    printf("base=0x%zX size=0x%X entry=0x%zX\n", image.base, image.size, image.entry_point);
    for (auto& s : image.sections)
        printf("section %-10s 0x%08zX size=0x%08X flags=%d\n", s.name.c_str(), s.base, s.size, s.flags);

    std::ofstream(argv[2], std::ios::binary).write((const char*)image.data.get(), image.size);

    FILE* f = fopen(argv[3], "w");
    for (auto& sym : image.symbols)
        fprintf(f, "0x%08zX 0x%zX %d %s\n", sym.address, sym.size, sym.type, sym.name.c_str());
    fclose(f);
    printf("symbols=%zu\n", image.symbols.size());
    return 0;
}
