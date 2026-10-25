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
import singleflow  # noqa: E402

CRLIVE = ppc2c.pass_on("WWHD_RECOMP_CRLIVE")
CR_CHECK = os.environ.get("WWHD_RECOMP_CR_CHECK", "0") == "1"
LEAF = ppc2c.pass_on("WWHD_RECOMP_LEAF")
NONLEAF = ppc2c.pass_on("WWHD_RECOMP_NONLEAF")
GQR_STATIC = ppc2c.pass_on("WWHD_RECOMP_GQR")
HOT = os.environ.get("RECOMP_HOT", "1") != "0"

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
        self.config_dir = config_dir
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
                if name in ("OSSaveContext", "OSLoadContext"):  # context switches: no calling convention
                    leaflocal.NO_ABI.update({"f_%08X" % addr, "f_%08X_orig" % addr})
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

    NO_ABI_NAMES = ("__restore_fpr", "__init_registers", "__OSLoadFPUContext", "OSLoadContext", "OSSwitchFPUContext",
                    "TRKLoadContext")

    def _load_dtk(self, path, rel):
        self.pending_jts = getattr(self, "pending_jts", [])
        self.trk_ranges = getattr(self, "trk_ranges", [])
        for s in read_symbols(path):
            # hand-written routines that set callee-saved registers on purpose: the FPR restore
            # routine (and its _restfpr_N entry labels) for the epilogue that calls it, register
            # set-up at boot, context and FPU context loads. A call there is not a calling-convention
            # call (leaflocal.py reloads every register after it).
            if s["name"] in self.NO_ABI_NAMES or s["name"].startswith("_restfpr_"):
                base, size = self._addr(s, rel), s["size"] or 4
                leaflocal.NO_ABI.update({"f_%08X" % a for a in range(base, base + size, 4)})
            if s["type"] == "function" and s["section"] in ("init", "text"):
                self.entries.add(self._addr(s, rel))
                if s["name"].startswith("TRK"):  # the MetroTRK debugger stub: never runs here
                    base = self._addr(s, rel)
                    self.trk_ranges.append((base, base + (s["size"] or 4)))
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
        if singleflow.ENABLED and not plain:
            def call_keeps(a, w):
                t = branch_target(a, w & ~1)  # the call's target (branch_target skips linking branches)
                return (t is not None and t in self.entries and t not in self.hooks
                        and "f_%08X" % t not in leaflocal.NO_ABI)
            body, seen, done = singleflow.analyze_and_rewrite(body, self.cur_start, self.cur_end, branch_target,
                                                              call_keeps, self.patches)
            self.single_stats[0] += seen
            self.single_stats[1] += done
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
        # a function that keeps r1 in a local: an interrupt delivered at a loop back-edge must see the
        # current stack pointer (its frame may already be below the struct's stale r1)
        if any("r1 = c->r[1]" in p for p in out[2:]):
            body = [(a, w, s.replace("PPC_LOOP();", "PPC_LOOP_R1(r1);")) for a, w, s in body]
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
        """GQRs that always hold 0 (plain floats): no mtspr writes anything else to them. Writes that
        do not count: a value set by `li rS, 0` just before (the OS's start-up clear), writes inside
        natively replaced functions (OSLoadContext) and in the MetroTRK debugger stub (never runs).
        Threads' contexts start with GQRs cleared, and OSLoadContext only restores saved values."""
        hooked = [(a, self.func_end(a)) for a in self.hooks]
        skip = hooked + self.trk_ranges
        written = set()
        for lo, hi, data in self.img.regions:
            for i in range(0, hi - lo, 4):
                w = struct.unpack_from(">I", data, i)[0]
                if w >> 26 == 31 and (w >> 1) & 0x3FF == 467:
                    spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
                    if 912 <= spr <= 919 or 896 <= spr <= 903:
                        a = lo + i
                        if any(s0 <= a < s1 for s0, s1 in skip):
                            continue
                        rs = (w >> 21) & 31
                        j = i - 4
                        while j >= 0:  # back over the run of mtspr before it
                            p = struct.unpack_from(">I", data, j)[0]
                            if not (p >> 26 == 31 and (p >> 1) & 0x3FF == 467):
                                break
                            j -= 4
                        if j >= 0 and struct.unpack_from(">I", data, j)[0] == (0x38000000 | rs << 21):  # li rS, 0
                            continue
                        written.add((spr - 912) if spr >= 912 else (spr - 896))
        return sum(1 << n for n in range(8) if n not in written)

    def run(self, outdir, per_file):
        os.makedirs(outdir, exist_ok=True)
        self.unhandled = collections.Counter()
        self.cr_stats = [0, 0]
        self.hot_count = 0
        self.single_stats = [0, 0]
        self.leaf_count = self.nonleaf_count = 0
        self.untabled_switches = []
        # hot functions first, in the list's order (config/GXEE8P/hot_functions.txt, tools/hot_functions.py):
        # the code a race runs then sits together in the program. RECOMP_HOT=0 keeps address order.
        hot = []
        hot_path = os.path.join(self.config_dir, "hot_functions.txt")
        if HOT and os.path.exists(hot_path):
            seen = set()
            for line in open(hot_path):
                line = line.split("#")[0].strip()
                if not line:
                    continue
                a = int(line, 16)  # an entry, or an address inside a function (a caller's return address)
                f = self.func_of(a) if self.img.in_text(a) else None
                if f is not None and a < self.func_end(f) and f not in seen:
                    seen.add(f)
                    hot.append(f)
        hot_set = set(hot)
        order = hot + [e for e in self.sorted_entries if e not in hot_set]
        self.hot_count = len(hot)
        files, cur, n = [], [], 0
        for k, start in enumerate(order):
            src, count = self.emit_function(start)
            cur.append(src)
            n += count
            if n >= per_file or k + 1 == len(hot):  # the hot functions get files of their own
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
            f.write("hot functions emitted first: %d\n" % self.hot_count)
            f.write("round25 multiplier operands: %d, left out as already single precision: %d%s\n" % (
                self.single_stats[0], self.single_stats[1], " (checking build)" if singleflow.CHECK else ""))
            f.write("unhandled instruction kinds (op=0: zero/data words inside function ranges, never run):\n")
            for k, v in self.unhandled.most_common():
                f.write("  %6d  %s\n" % (v, k))
            for note in ppc2c.REPORT_NOTES:
                f.write("note: %s\n" % note)
        print("recompiled %d functions into %d files (details: %s)" % (
            len(self.sorted_entries), nfiles, os.path.join(outdir, "report.txt").replace("\\", "/")))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("config_dir")
    ap.add_argument("baserom")
    ap.add_argument("outdir")
    ap.add_argument("--insns-per-file", type=int, default=20000)
    a = ap.parse_args()
    GcRecompiler(a.config_dir, a.baserom).run(a.outdir, a.insns_per_file)
