"""Dump XEX2 header info: base, entry, format, import libraries and import counts."""
import struct
import sys

KEYS = {
    0x000002FF: "resource_info", 0x000003FF: "file_format_info",
    0x000005FF: "delta_patch", 0x00010100: "entry_point",
    0x00010201: "image_base", 0x000103FF: "import_libraries",
    0x00018002: "checksum_timestamp", 0x000183FF: "original_pe_name",
    0x000200FF: "static_libraries", 0x00020104: "tls_info",
    0x00020200: "default_stack_size", 0x00020301: "fs_cache_size",
    0x00020401: "heap_size", 0x00030000: "system_flags",
    0x00040006: "execution_info", 0x00040201: "title_workspace",
    0x00040310: "game_ratings", 0x00040404: "lan_key",
    0x000405FF: "xbox360_logo", 0x00E10402: "alt_title_ids",
}

d = open(sys.argv[1], "rb").read()
be32 = lambda o: struct.unpack_from(">I", d, o)[0]
be16 = lambda o: struct.unpack_from(">H", d, o)[0]

magic, mflags, pe_off, _, sec_off, hcount = struct.unpack_from(">4sIIIII", d, 0)
print(f"magic={magic} module_flags=0x{mflags:X} pe_data_offset=0x{pe_off:X} headers={hcount}")
hdr = {}
for i in range(hcount):
    k, v = struct.unpack_from(">II", d, 0x18 + i * 8)
    hdr[k] = v
    print(f"  {KEYS.get(k, '?'):22} key=0x{k:08X} val=0x{v:08X}")

if 0x10100 in hdr:
    print(f"entry_point = 0x{hdr[0x10100]:08X}")
print(f"image_size  = 0x{be32(sec_off + 4):08X}")
print(f"load_address= 0x{be32(sec_off + 0x110):08X}")

if 0x3FF in hdr:
    o = hdr[0x3FF]
    enc, comp = be16(o + 4), be16(o + 6)
    print(f"encryption={['none', 'normal'][enc] if enc < 2 else enc} "
          f"compression={['none', 'basic', 'normal(LZX)', 'delta'][comp] if comp < 4 else comp}")
    if comp == 2:
        print(f"  LZX window size = 0x{be32(o + 8):X}")

if 0x40006 in hdr:
    o = hdr[0x40006]
    media, ver, base_ver, tid = struct.unpack_from(">IIII", d, o)
    print(f"title_id = {tid:08X}  version=0x{ver:08X}")

if 0x183FF in hdr:
    o = hdr[0x183FF]
    print("original_pe_name =", d[o + 4:o + be32(o)].split(b"\0")[0].decode())

if 0x200FF in hdr:
    o = hdr[0x200FF]
    n = (be32(o) - 4) // 16
    print("static libraries:")
    for i in range(n):
        e = o + 4 + i * 16
        name = d[e:e + 8].split(b"\0")[0].decode()
        maj, mnr, bld, qfe = struct.unpack_from(">HHHH", d, e + 8)
        print(f"  {name:10} {maj}.{mnr}.{bld}.{qfe & 0xFF}")

if 0x103FF in hdr:
    o = hdr[0x103FF]
    size, str_size, count = struct.unpack_from(">III", d, o)
    names = d[o + 12:o + 12 + str_size].split(b"\0")
    names = [n.decode() for n in names if n]
    p = o + 12 + str_size
    print("import libraries:")
    for i in range(count):
        lsize = be32(p)
        name_idx, rec_count = be16(p + 0x24), be16(p + 0x26)
        ver = be32(p + 0x1C)
        print(f"  {names[name_idx]:16} records={rec_count} "
              f"ver={ver >> 28}.{(ver >> 24) & 0xF}.{(ver >> 8) & 0xFFFF}")
        p += lsize
