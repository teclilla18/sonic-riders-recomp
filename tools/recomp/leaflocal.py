# Forked from ZeldaWWHDRecomp tools/recomp/leaflocal.py (commit f10b566), MPL-2.0 (see third_party/LICENSE-MPL-2.0).
# Modified for GameCube (Gekko) DOL/REL recompilation by the Sonic Riders port; see NOTES.md.
"""Keep a leaf function's guest registers in C locals.

ppc2c turns every register access into a Cpu struct access (c->r[N], c->f[N].psK). With
`Cpu* __restrict c` the C compiler keeps values in host registers inside a function, but it must
store every register the function changed back into the struct at each return: it cannot know that
callers ignore most of them. A leaf function (one that calls nothing) can instead use locals and,
at a return (blr), write back only what callers can see under the PowerPC EABI: the return values
(r3, r4, f1, f2), the stack pointer and small-data bases (r1, r2, r13) and the callee-saved
registers (r14-r31, f14-f31). Any other exit (a tail jump or dispatch to other code) writes back
every register the function changed.

The condition register gets the same treatment: each CR bit the function uses becomes a local, and a
return writes back only the callee-saved fields cr2-cr4 (bits 8-19), the ones crlive.py also treats
as live at a return. Compares write the locals through ppc.h's crl_* helpers. A function that reads
or writes the CR as a whole (mfcr, mtcrf, stwcx.) keeps its CR in the struct.

The statements are the same C expressions with the struct accesses replaced, so the results are
the same; only the dead stores of scratch registers go away. Not applied to functions with calls
(transform_nonleaf below handles those), hooks, instruction hooks, traps or unimplemented
instructions.
"""
import os
import re

# WWHD_RECOMP_LEAF_POISON=1: a checking build; at each return the scratch registers the function
# used get garbage, so a caller that relied on one would break visibly
POISON = os.environ.get("WWHD_RECOMP_LEAF_POISON", "0") == "1"

_R = re.compile(r"c->r\[(\d+)\]")
_F = re.compile(r"c->f\[(\d+)\]\.ps([01])")
_PSQ_LOAD = re.compile(r"\bpsq_load\(c, (\d+), ")
_PSQ_STORE = re.compile(r"\bpsq_store\(c, (\d+), ")
_CALL = re.compile(r"(?<!MUSTTAIL return )\b(f_[0-9A-F]{8}(?:_orig)?|imp_\w+|ppc_dispatch)\(c\b")
_BLOCKERS = ("site_", "ppc_unimplemented", "ppc_trap", "hook_")
_RET = re.compile(r"(?<![\w])return;")
_TAIL = re.compile(r"MUSTTAIL return [^;]*;")

_CR = re.compile(r"(?<!&)\bc->cr\[(\d+)\]")
_CR_ADDR = re.compile(r"&c->cr\[(\d+)\]")
_MCRF = re.compile(r"memmove\(&c->cr\[(\d+)\], &c->cr\[(\d+)\], 4\);")
_CR_SET = re.compile(r"\bcr_set_([suf])(_m)?\(c, (\d+), ")
_CR0_RC = re.compile(r"\bcr0_rc(_m)?\(c, ")
_CR_READ = re.compile(r"\bppc_cr_read\(c, (\d+), ")
_CR_LEFT = re.compile(r"c->cr\b|\bppc_mfcr\b|\bppc_mtcrf\b|\bppc_stwcx\b|\bcr_set_|\bcr0_rc|\bppc_cr_read\(")
_CR_LOCAL = re.compile(r"\bcr(\d+)\b")
RET_CR = set(range(8, 20))  # cr2-cr4


def _field_ptrs(f):
    return "&cr%d, &cr%d, &cr%d, &cr%d, c, " % (4 * f, 4 * f + 1, 4 * f + 2, 4 * f + 3)


def localize_cr(stmts):
    """The statements with CR bits as locals cr0..cr31, and the set of bits used; or None if the
    function uses the CR in a way this does not cover."""
    out = []
    for s in stmts:
        s = _MCRF.sub(lambda m: "{ %s %s }" % (
            " ".join("uint8_t t%d = c->cr[%d];" % (i, int(m.group(2)) + i) for i in range(4)),
            " ".join("c->cr[%d] = t%d;" % (int(m.group(1)) + i, i) for i in range(4))), s)
        s = _CR_SET.sub(lambda m: "crl_set_%s%s(%s" % (m.group(1), m.group(2) or "", _field_ptrs(int(m.group(3)))), s)
        s = _CR0_RC.sub(lambda m: "crl0_rc%s(%s" % (m.group(1) or "", _field_ptrs(0)), s)
        s = _CR_READ.sub(lambda m: "ppc_crl_read(c, cr%s, %s, " % (m.group(1), m.group(1)), s)
        s = _CR.sub(lambda m: "cr%s" % m.group(1), s)
        if _CR_LEFT.search(s) or _CR_ADDR.search(s):
            return None
        out.append(s)
    used = set()
    for s in out:
        used |= {int(n) for n in _CR_LOCAL.findall(s) if int(n) < 32}
    return out, used


RET_GPR = {1, 2, 3, 4, 13} | set(range(14, 32))
RET_FPR = {1, 2} | set(range(14, 32))


def eligible(stmts):
    for s in stmts:
        if _CALL.search(s) or any(b in s for b in _BLOCKERS):
            return False
    return True


def transform(stmts):
    """stmts: the function's statements (after PPC_ENTER). Returns (prologue, new statements,
    write-back for falling off the end into the next function)."""
    gprs, fprs = set(), set()
    out = []
    for s in stmts:
        s = _PSQ_LOAD.sub(lambda m: (fprs.add(int(m.group(1))), "psq_load_l(c, &f%s_0, &f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _PSQ_STORE.sub(lambda m: (fprs.add(int(m.group(1))), "psq_store_l(c, f%s_0, f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _R.sub(lambda m: (gprs.add(int(m.group(1))), "r%s" % m.group(1))[1], s)
        s = _F.sub(lambda m: (fprs.add(int(m.group(1))), "f%s_%s" % (m.group(1), m.group(2)))[1], s)
        out.append(s)
    crs = set()
    localized = localize_cr(out)
    if localized:
        out, crs = localized
    # every register the function uses is written back where callers may see it (written or not:
    # no reliance on spotting each assignment form in the C)
    gw, fw = gprs, fprs
    prologue = []
    if crs:
        prologue.append("uint8_t %s;" % ", ".join("cr%d = c->cr[%d]" % (n, n) for n in sorted(crs)))
    if gprs:
        prologue.append("uint32_t %s;" % ", ".join("r%d = c->r[%d]" % (n, n) for n in sorted(gprs)))
    if fprs:
        prologue.append("double %s;" % ", ".join("f%d_0 = c->f[%d].ps0, f%d_1 = c->f[%d].ps1" % (n, n, n, n) for n in sorted(fprs)))

    def wb(gset, fset, cset):
        parts = ["c->r[%d] = r%d;" % (n, n) for n in sorted(gset)]
        parts += ["c->f[%d].ps0 = f%d_0; c->f[%d].ps1 = f%d_1;" % (n, n, n, n) for n in sorted(fset)]
        parts += ["c->cr[%d] = cr%d;" % (n, n) for n in sorted(cset)]
        return " ".join(parts)

    ret_wb = wb(gw & RET_GPR, fw & RET_FPR, crs & RET_CR)
    if POISON:
        # only registers the function changes: a register it merely reads keeps its value in the
        # struct, as on the hardware
        text = "\n".join(out)
        g_set = {n for n in gprs if re.search(r"\br%d\s*(=[^=]|\|=|&=|\^=|\+=|-=|<<=|>>=)" % n, text)}
        f_set = {n for n in fprs if re.search(r"(\bf%d_[01]\s*=[^=]|&f%d_[01])" % (n, n), text)}
        ret_wb += " " + " ".join(["c->r[%d] = 0xDEADBEEFu;" % n for n in sorted(g_set - RET_GPR)] +
                                 ["c->f[%d].ps0 = c->f[%d].ps1 = __builtin_nan(\"\");" % (n, n) for n in sorted(f_set - RET_FPR)])
    all_wb = wb(gw, fw, crs)
    out = [_TAIL.sub(lambda m: "{ %s %s }" % (all_wb, m.group(0)) if all_wb else m.group(0),
                     _RET.sub(lambda m: "{ %s return; }" % ret_wb if ret_wb else "return;", s)) for s in out]
    return prologue, out, all_wb


# ---------------------------------------------------------------- functions with calls
# The same locals in a function that calls others. Before each call the registers the function
# assigned since the last sync go back to the Cpu struct (the callee may read any of them); after
# it, every register the function uses is read again. The callee-saved ones (r1, r14-r31, f14-f31)
# are read again too, although the PowerPC EABI has callees preserve them: a full save state may
# replace a thread's registers while it waits inside a call, and the compiler's save/restore
# helpers change them on purpose. Between calls the values stay in locals, so the C compiler can
# keep them in host registers and drop the stores of values that are overwritten before the next
# call or exit. Functions whose code passes the Cpu to anything else keep the struct form.
_NL_CALL = re.compile(r"(?<!MUSTTAIL return )\b(f_[0-9A-F]{8}(?:_orig)?|imp_\w+|ppc_dispatch|site_[0-9A-F]{8})\(c\);"
                      r"|\b(?:ppc_trap|ppc_unimplemented)\(c[^;]*\);")
_C_USE = re.compile(r"\b(\w+)\(c\b")
_NL_ALLOWED = {"cr_set_s", "cr_set_u", "cr_set_f", "cr_set_s_m", "cr_set_u_m", "cr_set_f_m", "cr0_rc", "cr0_rc_m",
               "psq_load_l", "psq_store_l", "ppc_mfcr", "ppc_mtcrf", "ppc_fctiw", "ppc_lwarx", "ppc_stwcx",
               "ppc_mfxer", "ppc_mtxer", "ppc_cr_read", "ppc_dispatch", "ppc_trap", "ppc_unimplemented"}


_ASSIGN_R = re.compile(r"\br(\d+)\s*(?:=(?!=)|\|=)|&r(\d+)\b")
_ASSIGN_F = re.compile(r"\bf(\d+)_[01]\s*=(?!=)|&f(\d+)_[01]\b")
_GOTO = re.compile(r"goto L_([0-9A-F]{8});")


def _assigned(text):
    regs = set()
    for m in _ASSIGN_R.finditer(text):
        regs.add(int(m.group(1) or m.group(2)))
    for m in _ASSIGN_F.finditer(text):
        regs.add(32 + int(m.group(1) or m.group(2)))
    return regs


# After a call, the callee-saved registers (r1, r2, r13, r14-r31, f14-f31) keep the caller's values:
# the EABI that the returns above already rely on. They are not read back from the struct then (the
# volatile ones still are; the C compiler drops those that are dead). RECOMP_CALLEE_SAVED=0 turns this
# off. NO_ABI: callees that do not follow the convention (context switches), filled by gcrecomp.py;
# indirect calls (ppc_dispatch) and instruction hooks (site_) also get the full reload.
CALLEE_SAVED = os.environ.get("RECOMP_CALLEE_SAVED", "1") != "0"
SAVED_GPR = {1, 2, 13} | set(range(14, 32))
SAVED_FPR = set(range(14, 32))
NO_ABI = set()


# Register summaries (RECOMP_SUMMARY=0 turns them off): for each function, every GPR/FPR that it or
# anything it calls (directly, by a tail jump or by falling through) mentions at all, read or written.
# A call to a function with a summary writes back only the dirty registers in it (the others stay
# dirty in locals) and reads back only the volatile ones in it: the callee cannot see or change the
# rest. Each guest thread has its own Cpu and interrupts run on a copy (sched.c), so nothing else reads
# the caller's registers from the struct meanwhile. A function with an indirect call, a native
# replacement, an instruction hook, an unknown instruction, a context switch (NO_ABI) or any helper
# that is handed the Cpu other than the ones below has no summary (full write-back and reload).
USE_SUMMARY = os.environ.get("RECOMP_SUMMARY", "1") != "0"
SUMMARY_CHECK = os.environ.get("RECOMP_SUMMARY_CHECK", "0") == "1"  # checking build (ppc.h ppc_sum_poison)
SUMMARY = {}  # "f_XXXXXXXX" -> frozenset of register ids (GPR n, FPR 32 + n); absent: unknown
SUMMARY_SAFE = {"cr_set_s", "cr_set_u", "cr_set_f", "cr_set_s_m", "cr_set_u_m", "cr_set_f_m", "cr0_rc", "cr0_rc_m",
                "psq_load", "psq_store", "ppc_mfcr", "ppc_mtcrf", "ppc_fctiw", "ppc_mfxer", "ppc_mtxer",
                "ppc_mfspr", "ppc_mtspr", "ppc_mfmsr", "ppc_dcbz", "ppc_lwarx", "ppc_stwcx", "ppc_cr_read"}
_S_CALL = re.compile(r"\b(f_[0-9A-F]{8}(?:_orig)?)\(c\);")
_S_USE = re.compile(r"\b(\w+)\(c\b(?!->)")


def summarize(stmts):
    """(registers mentioned, functions called or jumped to, known): from the struct-form statements"""
    regs, callees = set(), set()
    for s in stmts:
        regs |= {int(n) for n in _R.findall(s)}
        regs |= {32 + int(n) for n, _ in _F.findall(s)}
        regs |= {32 + int(n) for n in _PSQ_LOAD.findall(s)}
        regs |= {32 + int(n) for n in _PSQ_STORE.findall(s)}
        callees |= set(_S_CALL.findall(s))
        for m in _S_USE.finditer(s):
            name = m.group(1)
            if name in SUMMARY_SAFE or re.match(r"f_[0-9A-F]{8}(_orig)?$", name):
                continue
            return regs, callees, False
        if "hook_" in s or "site_" in s:
            return regs, callees, False
    return regs, callees, True


def solve_summaries(funcs):
    """funcs: name -> (regs, callees, known). Fills SUMMARY with the fixpoint over the call graph."""
    SUMMARY.clear()
    if not USE_SUMMARY:
        return 0
    unknown = {n for n, (_, _, k) in funcs.items() if not k or n in NO_ABI}
    cur = {n: set(r) for n, (r, _, _) in funcs.items()}
    changed = True
    while changed:
        changed = False
        for n, (_, callees, _) in funcs.items():
            if n in unknown:
                continue
            for t in callees:
                if t in unknown or t not in funcs:
                    unknown.add(n)
                    changed = True
                    break
                if not cur[t] <= cur[n]:
                    cur[n] |= cur[t]
                    changed = True
    for n in funcs:
        if n not in unknown:
            # r1 always: an interrupt delivered at the callee's entry puts its frame just below the
            # struct's r1 (sched.c make_irq_cpu), which must be the caller's current stack pointer
            SUMMARY[n] = frozenset(cur[n] | {1})
    return len(SUMMARY)


def _touch(name):
    if not USE_SUMMARY or not name or name in NO_ABI:
        return None
    return SUMMARY.get(name)


def transform_nonleaf(stmts, addrs):
    """stmts: the statements (site calls already prefixed to theirs), addrs: their guest addresses.
    Returns (prologue, statements, write-back for falling off the end), or None to keep the struct
    form. A forward analysis over the function's jumps tracks which registers may differ from the
    struct ("dirty"); a call writes back only those, an exit only those its caller can see."""
    if any("hook_" in s for s in stmts):
        return None
    gprs, fprs = set(), set()
    out = []
    for s in stmts:
        s = _PSQ_LOAD.sub(lambda m: (fprs.add(int(m.group(1))), "psq_load_l(c, &f%s_0, &f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _PSQ_STORE.sub(lambda m: (fprs.add(int(m.group(1))), "psq_store_l(c, f%s_0, f%s_1, " % (m.group(1), m.group(1)))[1], s)
        s = _R.sub(lambda m: (gprs.add(int(m.group(1))), "r%s" % m.group(1))[1], s)
        s = _F.sub(lambda m: (fprs.add(int(m.group(1))), "f%s_%s" % (m.group(1), m.group(2)))[1], s)
        out.append(s)
    for s in out:
        for m in _C_USE.finditer(s):
            name = m.group(1)
            if name not in _NL_ALLOWED and not re.match(r"(f_[0-9A-F]{8}(_orig)?|imp_\w+|site_[0-9A-F]{8})$", name):
                return None
    if not gprs and not fprs:
        return None
    used = gprs | {32 + n for n in fprs}
    n = len(out)
    index = {a: i for i, a in enumerate(addrs)}
    # per statement: segments between calls (assignments), the calls, successors
    segs, calls, succ = [], [], []
    for i, s in enumerate(out):
        parts, pos, cs = [], 0, []
        for m in _NL_CALL.finditer(s):
            parts.append(_assigned(s[pos:m.start()]) & used)
            cs.append(m)
            pos = m.end()
        parts.append(_assigned(s[pos:]) & used)
        segs.append(parts)
        calls.append(cs)
        nx = [i + 1] if i + 1 < n else []  # falling through (a superset after an unconditional jump: safe)
        nx += [index[int(t, 16)] for t in _GOTO.findall(s) if int(t, 16) in index]
        succ.append(nx)
    # loop heads (targets of backward jumps): the edges entering them from before write back what is
    # dirty, so registers set up before a loop are not written back again at each call inside it
    heads = {j for i in range(n) for j in succ[i] if j <= i}
    d_in = [None] * n
    d_in[0] = set()
    work = [0]
    while work:
        i = work.pop()
        d = set(d_in[i])
        for k, part in enumerate(segs[i]):
            d |= part
            if k < len(calls[i]):
                t = _touch(calls[i][k].group(1))
                d = set() if t is None else d - t  # written back, and the volatile ones read again
        for j in succ[i]:
            arrive = set() if (j in heads and j > i) else d  # a forward edge into a loop head syncs
            if d_in[j] is None or not arrive <= d_in[j]:
                d_in[j] = set(arrive) if d_in[j] is None else d_in[j] | arrive
                work.append(j)
    for i in range(n):
        if d_in[i] is None:
            d_in[i] = set(used)  # not reached by the analysis (cannot happen): write back everything

    def wb(regs):
        parts = ["c->r[%d] = r%d;" % (r, r) for r in sorted(r for r in regs if r < 32)]
        parts += ["c->f[%d].ps0 = f%d_0; c->f[%d].ps1 = f%d_1;" % (r - 32, r - 32, r - 32, r - 32)
                  for r in sorted(r for r in regs if r >= 32)]
        return " ".join(parts)

    def rl(gset, fset):
        parts = ["r%d = c->r[%d];" % (r, r) for r in sorted(gset)]
        parts += ["f%d_0 = c->f[%d].ps0; f%d_1 = c->f[%d].ps1;" % (r, r, r, r) for r in sorted(fset)]
        return " ".join(parts)

    reload_all = rl(gprs, fprs)
    reload_vol = rl(gprs - SAVED_GPR, fprs - SAVED_FPR) if CALLEE_SAVED else reload_all

    def reload_after(name):
        if not name or name == "ppc_dispatch" or name.startswith("site_") or name in NO_ABI:
            return reload_all
        t = _touch(name)
        if t is None:
            return reload_vol
        g = {r for r in t if r < 32}
        f = {r - 32 for r in t if r >= 32}
        if CALLEE_SAVED:
            return rl((gprs - SAVED_GPR) & g, (fprs - SAVED_FPR) & f)
        return rl(gprs & g, fprs & f)
    ret_regs = RET_GPR | {32 + r for r in RET_FPR}
    final = []
    for i, s in enumerate(out):
        d = set(d_in[i])
        exit_dirty = set(d)  # for returns and tail calls in this statement: a superset is safe
        pieces, pos = [], 0
        for k, m in enumerate(calls[i]):
            d |= segs[i][k]
            exit_dirty |= segs[i][k]
            pieces.append(s[pos:m.start()])
            t = _touch(m.group(1))
            call = m.group(0)
            if SUMMARY_CHECK and t is not None:
                keep = sum(1 << r for r in t)
                call = "{ PpcSumSave sv_; ppc_sum_poison(c, 0x%016Xull, &sv_); %s ppc_sum_check(c, 0x%016Xull, &sv_, 0x%08Xu); }" % (
                    keep, call, keep, addrs[i])
            pieces.append("{ %s %s %s }" % (wb(d if t is None else d & t), call, reload_after(m.group(1))))
            pos = m.end()
            d = set() if t is None else d - t
        pieces.append(s[pos:])
        exit_dirty |= segs[i][-1]
        s = "".join(pieces)
        # forward jumps into a loop head write back first (see heads above)
        # the write-backs are built only for statements that jump or exit (most do neither)
        if "goto L_" in s:
            sync = wb(exit_dirty)
            if sync:
                s = _GOTO.sub(lambda m: "{ %s %s }" % (sync, m.group(0))
                              if int(m.group(1), 16) in index and index[int(m.group(1), 16)] in heads and index[int(m.group(1), 16)] > i
                              else m.group(0), s)
        if "return" in s:
            ret_wb, all_wb = wb(exit_dirty & ret_regs), wb(exit_dirty)
            if POISON:
                # checking build: the volatile registers this function sets get garbage at its returns
                g_set = {r for r in exit_dirty if r < 32 and r not in RET_GPR}
                f_set = {r - 32 for r in exit_dirty if r >= 32 and (r - 32) not in RET_FPR}
                ret_wb = (ret_wb + " " + " ".join(["c->r[%d] = 0xDEADBEEFu;" % r for r in sorted(g_set)] +
                          ["c->f[%d].ps0 = c->f[%d].ps1 = __builtin_nan(\"\");" % (r, r) for r in sorted(f_set)])).strip()
            s = _TAIL.sub(lambda m: "{ %s %s }" % (all_wb, m.group(0)) if all_wb else m.group(0),
                          _RET.sub(lambda m: "{ %s return; }" % ret_wb if ret_wb else "return;", s))
        # falling through into a loop head: write back at the end of this statement
        if i + 1 < n and i + 1 in heads:
            d_end = set(d_in[i])
            for k, part in enumerate(segs[i]):
                d_end |= part
                if k < len(calls[i]):
                    t = _touch(calls[i][k].group(1))
                    d_end = set() if t is None else d_end - t
            if d_end:
                s += " " + wb(d_end)
        final.append(s)
    # falling off the end continues in the next function: what is dirty after the last statement
    last = set(d_in[-1])
    for k, part in enumerate(segs[-1]):
        last |= part
        if k < len(calls[-1]):
            t = _touch(calls[-1][k].group(1))
            last = set() if t is None else last - t
    prologue = []
    if gprs:
        prologue.append("uint32_t %s;" % ", ".join("r%d = c->r[%d]" % (r, r) for r in sorted(gprs)))
    if fprs:
        prologue.append("double %s;" % ", ".join("f%d_0 = c->f[%d].ps0, f%d_1 = c->f[%d].ps1" % (r, r, r, r) for r in sorted(fprs)))
    return prologue, final, wb(last)
