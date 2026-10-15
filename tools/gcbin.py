"""GameCube binary helpers: DOL and REL loading, dtk symbols.txt parsing.

Shared by the analysis scripts and the recompiler front end. Reads files from baserom/
(never committed); nothing here embeds game data.
"""
import re
import struct


class Dol:
    """A DOL image: up to 7 text and 11 data sections, plus bss."""

    def __init__(self, path):
        d = open(path, "rb").read()
        self.raw = d
        offs = struct.unpack_from(">18I", d, 0x00)
        addrs = struct.unpack_from(">18I", d, 0x48)
        sizes = struct.unpack_from(">18I", d, 0x90)
        self.bss_addr, self.bss_size, self.entry = struct.unpack_from(">3I", d, 0xD8)
        self.sections = []  # (kind, index, addr, bytes)
        for i in range(18):
            if sizes[i]:
                kind = "text" if i < 7 else "data"
                self.sections.append((kind, i if i < 7 else i - 7, addrs[i], d[offs[i]:offs[i] + sizes[i]]))

    def text_sections(self):
        return [(a, b) for k, _, a, b in self.sections if k == "text"]

    def read(self, addr, size):
        for _, _, a, b in self.sections:
            if a <= addr and addr + size <= a + len(b):
                return b[addr - a:addr - a + size]
        raise KeyError("address %08X not in DOL" % addr)

    def word(self, addr):
        return struct.unpack(">I", self.read(addr, 4))[0]


# REL relocation types (PPC ELF + the REL-specific control types)
R_PPC_NONE, R_PPC_ADDR32, R_PPC_ADDR24, R_PPC_ADDR16 = 0, 1, 2, 3
R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA = 4, 5, 6
R_PPC_ADDR14, R_PPC_ADDR14_BRTAKEN, R_PPC_ADDR14_BRNKTAKEN = 7, 8, 9
R_PPC_REL24, R_PPC_REL14 = 10, 11
R_DOLPHIN_NOP, R_DOLPHIN_SECTION, R_DOLPHIN_END = 201, 202, 203


class Rel:
    """A REL module. `link(base)` lays it out at `base` and applies every relocation
    (against itself and against the DOL, module 0), the way OSLink would."""

    def __init__(self, path):
        d = open(path, "rb").read()
        self.raw = d
        (self.id, _, _, self.num_sections, self.section_info_offset, self.name_offset, self.name_size,
         self.version, self.bss_size, self.rel_offset, self.imp_offset, self.imp_size) = struct.unpack_from(">12I", d, 0)
        (self.prolog_section, self.epilog_section, self.unresolved_section, self.bss_section) = struct.unpack_from(">4B", d, 0x30)
        self.prolog, self.epilog, self.unresolved = struct.unpack_from(">3I", d, 0x34)
        self.align, self.bss_align = struct.unpack_from(">2I", d, 0x40) if self.version >= 2 else (1, 1)
        self.fix_size = struct.unpack_from(">I", d, 0x48)[0] if self.version >= 3 else len(d)
        self.sections = []  # (index, file_offset, size, exec)
        for i in range(self.num_sections):
            o, s = struct.unpack_from(">II", d, self.section_info_offset + 8 * i)
            self.sections.append((i, o & ~3, s, bool(o & 1)))
        self.imports = []
        for i in range(self.imp_size // 8):
            self.imports.append(struct.unpack_from(">II", d, self.imp_offset + 8 * i))

    def relocations(self):
        """Yield (module_id, section, offset, type, target_section, addend)."""
        for mod, off in self.imports:
            p = off
            sec, cur = None, 0
            while True:
                o, t, s, a = struct.unpack_from(">HBBI", self.raw, p)
                p += 8
                if t == R_DOLPHIN_END:
                    break
                if t == R_DOLPHIN_SECTION:
                    sec, cur = s, 0
                    continue
                cur += o
                if t == R_DOLPHIN_NOP:
                    continue
                yield mod, sec, cur, t, s, a

    def layout(self, base):
        """Section addresses when the module image is placed at `base` and bss right after
        fix_size (aligned to bss_align). Returns ({section: addr}, bss_addr, image_end)."""
        addrs = {}
        for i, o, s, _ in self.sections:
            if o:
                addrs[i] = base + o
        bss_addr = (base + self.fix_size + self.bss_align - 1) & ~(self.bss_align - 1)
        for i, o, s, _ in self.sections:
            if s and not o:
                addrs[i] = bss_addr
        return addrs, bss_addr, bss_addr + self.bss_size

    def link(self, base, dol_resolve=lambda sec, add: add):
        """Return (image bytes from base up to fix_size, section addrs, bss addr, applied relocs).
        Module-0 (DOL) targets are absolute addresses in the addend."""
        addrs, bss_addr, _ = self.layout(base)
        img = bytearray(self.raw[:self.fix_size])
        applied = []
        for mod, sec, off, t, tsec, add in self.relocations():
            where = addrs[sec] + off
            p = where - base
            if mod == self.id:
                target = (addrs[tsec] + add) & 0xFFFFFFFF
            elif mod == 0:
                target = dol_resolve(tsec, add) & 0xFFFFFFFF
            else:
                raise ValueError("import from unknown module %d" % mod)
            w = struct.unpack_from(">I", img, p & ~3)[0] if t not in (R_PPC_ADDR16, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA) else 0
            if t == R_PPC_ADDR32:
                struct.pack_into(">I", img, p, target)
            elif t in (R_PPC_ADDR16_LO, R_PPC_ADDR16):
                struct.pack_into(">H", img, p, target & 0xFFFF)
            elif t == R_PPC_ADDR16_HI:
                struct.pack_into(">H", img, p, (target >> 16) & 0xFFFF)
            elif t == R_PPC_ADDR16_HA:
                struct.pack_into(">H", img, p, ((target + 0x8000) >> 16) & 0xFFFF)
            elif t == R_PPC_REL24:
                d = (target - where) & 0x03FFFFFC
                struct.pack_into(">I", img, p, (w & 0xFC000003) | d)
            elif t == R_PPC_REL14:
                d = (target - where) & 0xFFFC
                struct.pack_into(">I", img, p, (w & 0xFFFF0003) | d)
            elif t == R_PPC_ADDR24:
                struct.pack_into(">I", img, p, (w & 0xFC000003) | (target & 0x03FFFFFC))
            elif t == R_PPC_ADDR14:
                struct.pack_into(">I", img, p, (w & 0xFFFF0003) | (target & 0xFFFC))
            else:
                raise ValueError("unhandled REL relocation type %d at %08X" % (t, where))
            applied.append((where, t, target, mod))
        return bytes(img), addrs, bss_addr, applied


SYM_RE = re.compile(r"^(\S+) = \.([\w.$]+):0x([0-9A-Fa-f]+); // (.*)$")


def read_symbols(path):
    """dtk symbols.txt -> list of dicts {name, section, addr, size, type, attrs}."""
    out = []
    for line in open(path):
        m = SYM_RE.match(line.strip())
        if not m:
            continue
        attrs = {}
        for tok in m.group(4).split():
            k, _, v = tok.partition(":")
            attrs[k] = v
        out.append({
            "name": m.group(1), "section": m.group(2), "addr": int(m.group(3), 16),
            "size": int(attrs.get("size", "0"), 16), "type": attrs.get("type", ""), "attrs": attrs,
        })
    return out
