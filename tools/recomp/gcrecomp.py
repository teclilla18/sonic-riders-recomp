#!/usr/bin/env python3
"""Statically recompile a GameCube DOL plus its REL module into C.

usage: gcrecomp.py CONFIG_DIR BASEROM_DIR OUTDIR [--insns-per-file N]

Front end for the forked ZeldaWWHDRecomp translator (ppc2c.py, crlive.py, leaflocal.py):

- Function entries come from decomp-toolkit's analysis (CONFIG_DIR/symbols.txt and
  CONFIG_DIR/_Main/symbols.txt), which understands Metrowerks code: it finds functions through
  calls, data relocations and pointer tables. Branch targets that land inside another function are
  added as entries (a fixpoint, as upstream).
- Switch jump tables: Metrowerks emits them as tables of absolute code addresses in .data/.rodata
  (dtk names them jumptable_*). Each table's targets become labels of the function containing
  them, and an indirect `bctr` in that function switches over its targets.
- The REL is linked at a fixed address (REL_BASE): every relocation is applied here, the way
  OSLink would, and the runtime links the module at the same address (runtime/src/rel.c).
- Native replacements (HLE): CONFIG_DIR/hle.txt lists `ADDRESS NAME` lines; f_ADDRESS then calls the
  runtime's hle_NAME(c), and the game's code stays available as f_ADDRESS_orig.
- Code patches: CONFIG_DIR/patches.txt lists `ADDRESS before|after C-STATEMENT` lines (the statement
  in the translator's terms: c->r[n], c->f[n].ps0, ...; it is rewritten with the instruction's) and
  `decl C-DECLARATION` lines for runtime globals the statements use (emitted into funcs.h). Patches
  are the native form of Gecko code hooks; a runtime flag in the statement makes them switchable.

Output (all derived from the game: stays in build/, never committed):
  OUTDIR/funcs.h, OUTDIR/code_NNN.c, OUTDIR/table.c, OUTDIR/report.txt
"""
import argparse
import bisect
import collections
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
from gcbin import Dol, Rel, read_symbols  # noqa: E402
from ppc2c import translate, Unhandled  # noqa: E402
import ppc2c  # noqa: E402
import crlive  # noqa: E402
import leaflocal  # noqa: E402

CRLIVE = ppc2c.pass_on("WWHD_RECOMP_CRLIVE")
CR_CHECK = os.environ.get("WWHD_RECOMP_CR_CHECK", "0") == "1"
LEAF = ppc2c.pass_on("WWHD_RECOMP_LEAF")
NONLEAF = ppc2c.pass_on("WWHD_RECOMP_NONLEAF")
GQR_STATIC = ppc2c.pass_on("WWHD_RECOMP_GQR")

# Where the REL module lives in guest memory: just above the 24 MiB of MEM1 (the runtime backs a few
# extra MiB there; development hardware had 48 MiB at these addresses). The game still loads and
# "links" its own copy from its heap, as on the console, so it keeps exactly its normal memory; the
# native OSLink points that copy's header at this pre-linked image (runtime/src/rel.c).
REL_BASE = 0x81800000


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def branch_target(addr, w):
    op = w >> 26
    if op == 18 and not (w & 1):
        return (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else addr)) & 0xFFFFFFFF
    if op == 16 and not (w & 1):
        return (sext(w & 0xFFFC, 16) + (0 if w & 2 else addr)) & 0xFFFFFFFF
    return None


class Image:
    """All guest code: the DOL's text sections and the linked REL's executable section."""

    def __init__(self, dol, rel_img, rel_addrs, rel):
        self.regions = []  # (lo, hi, bytes)
        for lo, data in sorted(dol.text_sections()):
            if self.regions and self.regions[-1][1] == lo:  # merge .init and .text
                plo, _, pdata = self.regions.pop()
                self.regions.append((plo, lo + len(data), pdata + data))
            else:
                self.regions.append((lo, lo + len(data), data))
        self.rel_text = []
        for i, off, size, ex in rel.sections:
            if ex and size:
                lo = rel_addrs[i]
                data = rel_img[off:off + size]
                self.regions.append((lo, lo + size, data))
                self.rel_text.append((lo, lo + size))
        self.regions.sort()
        self.los = [r[0] for r in self.regions]

    def region(self, a):
        i = bisect.bisect_right(self.los, a) - 1
        if i >= 0 and a < self.regions[i][1]:
            return self.regions[i]
        return None

    def in_text(self, a):
        return self.region(a) is not None

    def word(self, a):
        lo, hi, data = self.region(a)
        return struct.unpack_from(">I", data, a - lo)[0]

    def region_end(self, a):
        return self.region(a)[1]


class GcRecompiler:
    def __init__(self, config_dir, baserom):
        self.dol = Dol(os.path.join(baserom, "main.dol"))
        self.rel = Rel(os.path.join(baserom, "_Main.rel"))
        rel_img, self.rel_addrs, self.rel_bss, self.rel_relocs = self.rel.link(REL_BASE)
        self.rel_img = rel_img
        self.img = Image(self.dol, rel_img, self.rel_addrs, self.rel)

        self.entries = set()
        self.jt_targets = collections.defaultdict(set)  # function start -> case targets
        self.jt_count = 0
        self._load_dtk(os.path.join(config_dir, "symbols.txt"), rel=False)
        self._load_dtk(os.path.join(config_dir, "_Main", "symbols.txt"), rel=True)
        self.entries.add(self.dol.entry)
        self.hooks = {}  # address -> hle name
        hle = os.path.join(config_dir, "hle.txt")
        if os.path.exists(hle):
            for ln, line in enumerate(open(hle), 1):
                line = line.split("#")[0].strip()
                if not line:
                    continue
                addr, name = line.split()[:2]
                addr = int(addr, 16)
                if addr not in self.entries:
                    raise SystemExit("%s:%d: %08X is not a function entry" % (hle, ln, addr))
                self.hooks[addr] = name
        self.patches = collections.defaultdict(lambda: ["", ""])  # address -> [before, after]
        self.patch_decls = []
        pf = os.path.join(config_dir, "patches.txt")
        if os.path.exists(pf):
            for ln, line in enumerate(open(pf), 1):
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if line.startswith("decl "):
                    self.patch_decls.append(line[5:].strip())
                    continue
                addr, where, stmt = line.split(None, 2)
                if where not in ("before", "after"):
                    raise SystemExit("%s:%d: expected before/after" % (pf, ln))
                self.patches[int(addr, 16)][where == "after"] += " " + stmt
        self._fixpoint()
        self._assign_jump_tables()

    # REL section names in dtk's symbols.txt -> REL section index
    REL_SECTIONS = {"text": 1, "rodata": 2, "data": 3, "bss": 4}

    def _addr(self, sym, rel):
        if not rel:
            return sym["addr"]
        sec = self.REL_SECTIONS[sym["section"]]
        return self.rel_addrs[sec] + sym["addr"]

    def _load_dtk(self, path, rel):
        self.pending_jts = getattr(self, "pending_jts", [])
        for s in read_symbols(path):
            if s["type"] == "function" and s["section"] in ("init", "text"):
                self.entries.add(self._addr(s, rel))
            elif s["type"] == "object" and s["size"] >= 4 and s["size"] % 4 == 0 and \
                    s["section"] in ("data", "rodata") and s["addr"] % 4 == 0:
                # candidate: dtk names only some switch tables jumptable_* (others keep names from
                # signatures, e.g. "@740"); _assign_jump_tables keeps the ones that are really tables
                self.pending_jts.append((self._addr(s, rel), s["size"], rel))

    def data_word(self, a, rel):
        if rel:
            return struct.unpack_from(">I", self.rel_img, a - REL_BASE)[0]
        return self.dol.word(a)

    def _assign_jump_tables(self):
        """A switch table: every word is a code address inside one function and not a function
        entry (tables of function pointers point at entries)."""
        for base, size, rel in self.pending_jts:
            words = [self.data_word(base + 4 * k, rel) for k in range(size // 4)]
            if not all(self.img.in_text(t) and t % 4 == 0 and t not in self.entries for t in words):
                continue
            funcs = {self.func_of(t) for t in words}
            if len(funcs) != 1:
                continue
            self.jt_count += 1
            self.jt_targets[funcs.pop()].update(words)

    def _bounds(self):
        self.sorted_entries = sorted(self.entries)

    def func_of(self, a):
        i = bisect.bisect_right(self.sorted_entries, a) - 1
        return self.sorted_entries[i] if i >= 0 else None

    def func_end(self, start):
        i = bisect.bisect_right(self.sorted_entries, start)
        end = self.img.region_end(start)
        if i < len(self.sorted_entries):
            end = min(end, self.sorted_entries[i])
        return end

    def _fixpoint(self):
        # every direct call target is an entry (Metrowerks' _savegpr_NN/_savefpr_NN helpers are
        # called in the middle of one routine that dtk sees as a single function)
        for lo, hi, data in self.img.regions:
            for i in range(0, hi - lo, 4):
                w = struct.unpack_from(">I", data, i)[0]
                if (w >> 26) == 18 and (w & 1):
                    t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else lo + i)) & 0xFFFFFFFF
                    if self.img.in_text(t):
                        self.entries.add(t)
        rounds = 0
        while True:
            self._bounds()
            new = set()
            for lo, hi, data in self.img.regions:
                for i in range(0, hi - lo, 4):
                    a = lo + i
                    t = branch_target(a, struct.unpack_from(">I", data, i)[0])
                    if t is None or not self.img.in_text(t) or t in self.entries:
                        continue
                    if self.func_of(t) != self.func_of(a):
                        new.add(t)
            rounds += 1
            if not new:
                break
            self.entries |= new
        self.fixpoint_rounds = rounds
        self._bounds()

    # --- callbacks used by ppc2c.translate ---
    imm_override = {}

    def branch(self, addr, tgt):
        if self.cur_start <= tgt < self.cur_end:
            self.labels.add(tgt)
            if tgt <= addr:
                return "PPC_LOOP(); goto L_%08X;" % tgt
            return "goto L_%08X;" % tgt
        if tgt in self.entries:
            return "MUSTTAIL return f_%08X(c);" % tgt
        return "c->pc = 0x%08Xu; MUSTTAIL return ppc_dispatch(c);" % tgt

    def call(self, addr, tgt):
        if tgt in self.entries:
            return "f_%08X(c);" % tgt
        return "c->pc = 0x%08Xu; ppc_dispatch(c);" % tgt

    def ret(self):
        return "return;"

    def indirect_jump(self, addr):
        targets = sorted(t for t in self.jt_targets.get(self.cur_start, ()) if self.cur_start <= t < self.cur_end)
        if targets:
            for t in targets:
                self.labels.add(t)
            back = any(t <= addr for t in targets)
            cases = " ".join("case 0x%08Xu: goto L_%08X;" % (t, t) for t in targets)
            return "%sswitch (c->ctr) { %s } c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);" % (
                "PPC_LOOP(); " if back else "", cases)
        # `lwzx rY, ...; mtctr rY; bctr` is Metrowerks' switch idiom: with no table found, it would
        # reach the dispatcher with a mid-function address at run time
        w1, w2 = self.img.word(addr - 4), self.img.word(addr - 8)
        if (w1 & 0xFC1FFFFF) == 0x7C0903A6 and w2 >> 26 == 31 and (w2 >> 1) & 0x3FF == 23:
            self.untabled_switches.append(addr)
        return "c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);"

    # --- emission ---
    def emit_function(self, start):
        self.cur_start, self.cur_end = start, self.func_end(start)
        self.labels = set()
        body = []
        for a in range(start, self.cur_end, 4):
            w = self.img.word(a)
            try:
                s = translate(a, w, self)
            except Unhandled as e:
                self.unhandled[str(e)] += 1
                s = "ppc_unimplemented(c, 0x%08Xu, 0x%08Xu);" % (a, w)
            if a in self.patches:
                pre, post = self.patches[a]
                s = "{%s %s%s }" % (pre, s, post)
            body.append((a, w, s))
        hooked = start in self.hooks
        plain = hooked
        jts = {self.cur_start: (0, 0)}  # crlive only needs to know a bctr leaves the chunk
        if CRLIVE and not plain:
            live_after, fields = crlive.analyze(body, self.cur_start, self.cur_end, {}, branch_target, set())
            body = [(a, w, crlive.rewrite(s, fields[i], live_after[i], CR_CHECK, a)) for i, (a, w, s) in enumerate(body)]
            self.cr_stats[0] += sum(1 for f in fields if f is not None)
        del jts
        fname = "f_%08X_orig" % start if hooked else "f_%08X" % start
        out = []
        if hooked:
            out.append("void f_%08X(Cpu* __restrict c) { hle_%s(c); }\n" % (start, self.hooks[start]))
        out += ["void %s(Cpu* __restrict c) {" % fname, "    PPC_ENTER(0x%08Xu);" % start]
        tail_wb = ""
        stmts = [s for _, _, s in body]
        if LEAF and not plain and leaflocal.eligible(stmts):
            prologue, stmts, tail_wb = leaflocal.transform(stmts)
            body = [(a, w, s) for (a, w, _), s in zip(body, stmts)]
            out += ["    %s" % p for p in prologue]
            self.leaf_count += 1
        elif NONLEAF and not plain:
            done = leaflocal.transform_nonleaf(stmts, [a for a, _, _ in body])
            if done:
                prologue, stmts, tail_wb = done
                body = [(a, w, s) for (a, w, _), s in zip(body, stmts)]
                out += ["    %s" % p for p in prologue]
                self.nonleaf_count += 1
        for a, w, s in body:
            if a in self.labels:
                out.append("L_%08X: ;" % a)
            out.append("    %s /* %08X: %08X */" % (s, a, w))
        if self.cur_end < self.img.region_end(start):
            nxt = ("f_%08X_orig" if self.cur_end in self.hooks else "f_%08X") % self.cur_end
            out.append("    %sMUSTTAIL return %s(c);" % (tail_wb + " " if tail_wb else "", nxt))
        else:
            out.append("    ppc_unimplemented(c, 0x%08Xu, 0); /* fell off end of text */" % self.cur_end)
        out.append("}")
        return "\n".join(out), len(body)

    def static_float_gqrs(self):
        written = set()
        for lo, hi, data in self.img.regions:
            for i in range(0, hi - lo, 4):
                w = struct.unpack_from(">I", data, i)[0]
                if w >> 26 == 31 and (w >> 1) & 0x3FF == 467:
                    spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
                    if 912 <= spr <= 919 or 896 <= spr <= 903:
                        written.add((spr - 912) if spr >= 912 else (spr - 896))
        return sum(1 << n for n in range(8) if n not in written)

    def run(self, outdir, per_file):
        os.makedirs(outdir, exist_ok=True)
        self.unhandled = collections.Counter()
        self.cr_stats = [0, 0]
        self.leaf_count = self.nonleaf_count = 0
        self.untabled_switches = []
        files, cur, n = [], [], 0
        for start in self.sorted_entries:
            src, count = self.emit_function(start)
            cur.append(src)
            n += count
            if n >= per_file:
                files.append(cur)
                cur, n = [], 0
        if cur:
            files.append(cur)
        for i, funcs in enumerate(files):
            path = os.path.join(outdir, "code_%03d.c" % i)
            text = '#include "funcs.h"\n\n' + "\n\n".join(funcs) + "\n"
            if not os.path.exists(path) or open(path).read() != text:  # keep mtimes for ninja
                open(path, "w").write(text)
        self.write_headers(outdir)
        self.write_report(outdir, len(files))

    def write_headers(self, outdir):
        gqr = "#define PPC_GQR_STATIC_FLOAT 0x%02X\n" % self.static_float_gqrs() if GQR_STATIC else ""
        lines = ['#pragma once\n%s%s#include "ppc.h"\n' % ("#define PPC_CR_CHECK 1\n" if CR_CHECK else "", gqr)]
        for e in self.sorted_entries:
            lines.append("void f_%08X(Cpu* __restrict c);" % e)
        if self.patch_decls:
            lines.append("\n/* runtime state used by code patches (config/GXEE8P/patches.txt) */")
            lines += self.patch_decls
        lines.append("\n/* native replacements (config/GXEE8P/hle.txt) */")
        for a, name in sorted(self.hooks.items()):
            lines.append("void f_%08X_orig(Cpu* __restrict c);\nvoid hle_%s(Cpu* c);" % (a, name))
        open(os.path.join(outdir, "funcs.h"), "w").write("\n".join(lines) + "\n")

        t = ['#include "funcs.h"\n#include "recomp_table.h"\n']
        t.append("const RecompEntry g_recomp_funcs[] = {")
        t += ["    {0x%08Xu, f_%08X}," % (e, e) for e in self.sorted_entries]
        t.append("};\nconst unsigned g_recomp_func_count = %d;" % len(self.sorted_entries))
        t.append("const RecompHle g_recomp_hle[] = {")
        t += ['    {0x%08Xu, "%s", f_%08X_orig},' % (a, n, a) for a, n in sorted(self.hooks.items())]
        t.append("    {0u, 0, 0}};\nconst unsigned g_recomp_hle_count = %d;" % len(self.hooks))
        t.append("const uint32_t g_recomp_entry_point = 0x%08Xu;" % self.dol.entry)
        t.append("const uint32_t g_rel_base = 0x%08Xu, g_rel_bss = 0x%08Xu, g_rel_bss_size = 0x%08Xu;" % (
            REL_BASE, self.rel_bss, self.rel.bss_size))
        t.append("const uint32_t g_rel_prolog = 0x%08Xu;" % (self.rel_addrs[self.rel.prolog_section] + self.rel.prolog))
        t.append("const RecompRange g_code_ranges[] = {%s};\nconst unsigned g_code_range_count = %d;" % (
            ", ".join("{0x%08Xu, 0x%08Xu}" % (lo, hi) for lo, hi, _ in self.img.regions), len(self.img.regions)))
        open(os.path.join(outdir, "table.c"), "w").write("\n".join(t) + "\n")

    def write_report(self, outdir, nfiles):
        with open(os.path.join(outdir, "report.txt"), "w") as f:
            f.write("REL linked at %08X (bss %08X, %d relocations), prolog %08X\n" % (
                REL_BASE, self.rel_bss, len(self.rel_relocs),
                self.rel_addrs[self.rel.prolog_section] + self.rel.prolog))
            for lo, hi, _ in self.img.regions:
                f.write("code %08X-%08X\n" % (lo, hi))
            f.write("functions: %d (fixpoint rounds %d)\nfiles: %d\n" % (
                len(self.sorted_entries), self.fixpoint_rounds, nfiles))
            f.write("jump tables: %d, case targets: %d\n" % (self.jt_count, sum(len(v) for v in self.jt_targets.values())))
            f.write("native replacements: %d\n" % len(self.hooks))
            f.write("switch dispatches without a jump table: %d%s\n" % (
                len(self.untabled_switches), "".join(" %08X" % a for a in self.untabled_switches[:40])))
            f.write("CR writers rewritten: %d\nleaf functions in locals: %d\nnon-leaf in locals: %d\n" % (
                self.cr_stats[0], self.leaf_count, self.nonleaf_count))
            f.write("unhandled instruction kinds:\n")
            for k, v in self.unhandled.most_common():
                f.write("  %6d  %s\n" % (v, k))
        print(open(os.path.join(outdir, "report.txt")).read())


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("config_dir")
    ap.add_argument("baserom")
    ap.add_argument("outdir")
    ap.add_argument("--insns-per-file", type=int, default=20000)
    a = ap.parse_args()
    GcRecompiler(a.config_dir, a.baserom).run(a.outdir, a.insns_per_file)
