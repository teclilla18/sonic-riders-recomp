"""Single-precision tracking: leave out round25() where its operand is already single precision.

fmuls, fmadds and the paired-single multiplies round their multiplier operand to 25 mantissa bits
(round25 in ppc.h, the Gekko's behaviour). round25 is the identity on any value that is exactly
representable as a single (including infinities, NaNs from float loads and single denormals), and
that is what most operands are: they come from lfs, psq_l or an earlier single-precision result
(to_single). On ARM each round25 moves the value to an integer register and back.

This pass follows, through each function's branches, which register halves (ps0, ps1 of f0-f31)
are known to hold single-precision values (forward dataflow, intersected where paths meet), working
on the generated C text of each instruction:
  - `psq_load(c, D, ...)`, `{ double v = to_single(...)|ldf32(...); D.ps0 = v; D.ps1 = v; }` make
    both halves of D single;
  - `{ double v0 = E0, v1 = E1; D.ps0 = v0; D.ps1 = v1; }` (paired results, moves, merges): each
    half is single if its expression is to_single(...), or copies the state of a copied half
    (X, -X, fabs(X), -fabs(X));
  - `D.ps0 = X` (fmr, fneg, fabs, fnabs) copies; any other assignment makes the half unknown;
  - after a direct call to a function that follows the calling convention, ps0 of f14-f31 keeps its
    state (callee-saved doubles); everything else becomes unknown, and after any other call
    (indirect, dispatch, hooked or convention-breaking callees) everything does;
  - instructions with a recompiler patch, and code not reachable from the entry, know nothing.
round25(X) with X known single becomes (X), or ppc_single_chk(X, addr) when checking
(RECOMP_SINGLE_CHECK=1: the runtime logs any operand that was not single after all).
RECOMP_SINGLE=0 turns the pass off. Results are identical by construction.

Single-precision arithmetic in float (RECOMP_FLOAT=0 off): `to_single(A op B)` with op one of + - * /
and both A and B register halves known single becomes `(double)((float)A op (float)B)`. For these
operations on single-precision operands, rounding the exact result to double and then to single gives
the same value as rounding it to single directly (double has more than 2 * 24 + 2 bits), so the
results are bit-identical; the C compiler then drops the float<->double conversions between such
operations (two per single-precision instruction on ARM). Fused forms (fmadds etc.) stay in double.
RECOMP_FLOAT_CHECK=1 computes both and logs any difference (ppc.h ppc_float_chk).
Off by default (RECOMP_FLOAT=1 turns it on): checked identical on PC (attract demo and a menu route, 7,379
operations), but GCC keeps the register locals as doubles, so the Switch code had slightly more
conversions (one hot file: 5,328 fcvt vs 5,263). It would need the locals themselves typed float.
"""
import os
import re

ENABLED = os.environ.get("RECOMP_SINGLE", "1") != "0"
CHECK = os.environ.get("RECOMP_SINGLE_CHECK", "0") == "1"
FLOAT = os.environ.get("RECOMP_FLOAT", "0") == "1"  # off: GCC kept the doubles (see the docstring)
FLOAT_CHECK = os.environ.get("RECOMP_FLOAT_CHECK", "0") == "1"
_HALF_PAT = r"(?:\((c->f\[(\d+)\]\.ps([01]))\)|(c->f\[(\d+)\]\.ps([01])))"  # (half) or half, balanced
R_BINOP = re.compile(r"to_single\(" + _HALF_PAT + r" ([-+*/]) " + _HALF_PAT + r"\)")

ALL = (1 << 64) - 1


def bit(n, k):
    return 1 << (2 * n + k)


CALLEE_PS0 = sum(bit(n, 0) for n in range(14, 32))

R_ASSIGN = re.compile(r"c->f\[(\d+)\]\.ps([01]) = ")
R_PSQ = re.compile(r"psq_load\(c, (\d+),")
R_R25 = re.compile(r"round25\((c->f\[(\d+)\]\.ps([01]))\)")
R_GOTO = re.compile(r"goto L_([0-9A-F]{8});")
R_HALF = re.compile(r"^c->f\[(\d+)\]\.ps([01])$")
R_PAIR = re.compile(r"\{ double v0 = ([^;]*); c->f\[(\d+)\]\.ps0 = v0; c->f\[\2\]\.ps1 = v1; \}")
R_SAME = re.compile(r"\{ double v = ([^;]*); c->f\[(\d+)\]\.ps0 = v; c->f\[\2\]\.ps1 = v; \}")
R_ONE = re.compile(r"^c->f\[(\d+)\]\.ps0 = ([^;]*);")


def _split_top(text):
    """'A, v1 = B' -> ('A', 'B') at the top-level comma before ' v1 = '."""
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0 and text.startswith(", v1 = ", i):
            return text[:i], text[i + len(", v1 = "):]
    return None


def _strip_outer(e):
    e = e.strip()
    while e.startswith("(") and e.endswith(")"):
        depth = 0
        ok = True
        for i, ch in enumerate(e):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0 and i < len(e) - 1:
                ok = False
                break
        if not ok:
            break
        e = e[1:-1].strip()
    return e


def _expr_state(e, state):
    """Is the expression's value single precision, given the state before the instruction?"""
    e = _strip_outer(e)
    if e.startswith("to_single(") and e.endswith(")"):
        # to_single(...) as the whole expression (not to_single(a) + b)
        depth = 0
        for i, ch in enumerate(e):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0 and i >= len("to_single"):
                return i == len(e) - 1
        return False
    if (e.startswith("ldf32(") or e.startswith("ldf32r(")) and e.endswith(")"):  # lfs (r: stack/small data)
        return True
    for pre, post in (("-fabs(", ")"), ("fabs(", ")"), ("-", "")):
        if e.startswith(pre) and e.endswith(post):
            inner = e[len(pre):len(e) - len(post)] if post else e[len(pre):]
            m = R_HALF.match(inner.strip())
            if m:
                return bool(state & bit(int(m.group(1)), int(m.group(2))))
            return False
    m = R_HALF.match(e)
    if m:
        return bool(state & bit(int(m.group(1)), int(m.group(2))))
    return False


def transfer(s, state):
    m = R_PSQ.search(s)
    if m:
        d = int(m.group(1))
        return state | bit(d, 0) | bit(d, 1)
    written = {(int(a), int(b)) for a, b in R_ASSIGN.findall(s)}
    if not written:
        return state
    new = state
    known = {}
    m = R_SAME.search(s)
    if m:
        single = _expr_state(m.group(1), state)
        d = int(m.group(2))
        known[(d, 0)] = known[(d, 1)] = single
    else:
        m = R_PAIR.search(s)
        if m:
            parts = _split_top(m.group(1))
            d = int(m.group(2))
            if parts:
                known[(d, 0)] = _expr_state(parts[0], state)
                known[(d, 1)] = _expr_state(parts[1], state)
        else:
            m = R_ONE.match(s)
            if m and len(written) == 1:
                known[(int(m.group(1)), 0)] = _expr_state(m.group(2), state)
    for n, k in written:
        if known.get((n, k)):
            new |= bit(n, k)
        else:
            new &= ~bit(n, k)
    return new


def analyze_and_rewrite(body, start, end, branch_target, call_keeps, patched):
    """body: list of (addr, word, c_source). call_keeps(addr, word) -> True when a call there keeps
    the callee-saved ps0 halves. Returns (new body, operands seen, operands rewritten)."""
    n = len(body)
    index = {a: i for i, (a, _, _) in enumerate(body)}
    succ = [[] for _ in range(n)]
    after_call = [None] * n  # mask applied after the instruction (calls)
    for i, (a, w, s) in enumerate(body):
        op = w >> 26
        lk = w & 1
        fall = i + 1 < n
        if op == 18 and not lk:
            fall = False
        elif op == 16 and not lk and ((w >> 21) & 0x14) == 0x14:
            fall = False
        elif op == 19 and ((w >> 1) & 0x3FF) in (16, 528) and not lk and ((w >> 21) & 0x14) == 0x14:
            fall = False
        if lk and op in (16, 18, 19):
            after_call[i] = CALLEE_PS0 if (op != 19 and call_keeps(a, w)) else 0
        tg = [index[int(t, 16)] for t in R_GOTO.findall(s) if int(t, 16) in index]
        succ[i] = ([i + 1] if fall else []) + tg
    # reachability from the entry
    reach = [False] * n
    stack = [0] if n else []
    while stack:
        i = stack.pop()
        if reach[i]:
            continue
        reach[i] = True
        stack.extend(j for j in succ[i] if not reach[j])
    preds = [[] for _ in range(n)]
    for i in range(n):
        if reach[i]:
            for j in succ[i]:
                preds[j].append(i)
    st_in = [ALL if reach[i] else 0 for i in range(n)]
    st_out = [ALL if reach[i] else 0 for i in range(n)]
    if n:
        st_in[0] = 0
    changed = True
    while changed:
        changed = False
        for i in range(n):
            if not reach[i]:
                continue
            if i == 0:
                sin = 0
            else:
                sin = ALL
                for p in preds[i]:
                    sin &= st_out[p]
                if not preds[i]:
                    sin = 0
            a, w, s = body[i]
            if a in patched:
                sout = 0
            else:
                sout = transfer(s, sin)
                if after_call[i] is not None:
                    sout &= after_call[i]
            if sin != st_in[i] or sout != st_out[i]:
                st_in[i], st_out[i] = sin, sout
                changed = True
    seen = done = 0
    out = []
    for i, (a, w, s) in enumerate(body):
        if FLOAT and "to_single(" in s and reach[i] and a not in patched:
            state = st_in[i]

            def frep(m, state=state, a=a):
                ax, an, ak = (m.group(1), m.group(2), m.group(3)) if m.group(1) else (m.group(4), m.group(5), m.group(6))
                bx, bn, bk = (m.group(8), m.group(9), m.group(10)) if m.group(8) else (m.group(11), m.group(12), m.group(13))
                op = m.group(7)
                if not (state & bit(int(an), int(ak)) and state & bit(int(bn), int(bk))):
                    return m.group(0)
                fl = "((double)((float)%s %s (float)%s))" % (ax, op, bx)
                if FLOAT_CHECK:
                    return "ppc_float_chk(to_single(%s %s %s), %s, 0x%08Xu)" % (ax, op, bx, fl, a)
                return fl

            s = R_BINOP.sub(frep, s)
        if "round25(" in s:
            state = st_in[i] if reach[i] and a not in patched else 0

            def rep(m, state=state, a=a):
                nonlocal done
                if state & bit(int(m.group(2)), int(m.group(3))):
                    done += 1
                    return ("ppc_single_chk(%s, 0x%08Xu)" % (m.group(1), a)) if CHECK else "(%s)" % m.group(1)
                return m.group(0)

            seen += s.count("round25(")
            s = R_R25.sub(rep, s)
        out.append((a, w, s))
    return out, seen, done
