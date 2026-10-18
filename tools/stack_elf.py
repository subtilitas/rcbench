"""The part of tools/stack_check.py both its halves share: the reader of a
32-bit little-endian ELF (Executable and Linkable Format) file, the deepest
chain through a call graph, and what both read out of C source and out of
the tables in the docs.

stack_check.py holds the panel's task stacks to their deepest call chain
and stack_iomcu.py the IOmcu's two; stack_check.py's docstring says
how.

SPDX-License-Identifier: MIT
"""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def die(msg: str) -> None:
    sys.exit(f"stack_check: {msg}")


# --------------------------------------------------------------- the ELF --

class Elf:
    """The sections with contents, and the symbol table, of a 32-bit
    little-endian ELF."""

    def __init__(self, path: Path) -> None:
        d = path.read_bytes()
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            die(f"{path} is not a 32-bit little-endian ELF")
        self.data = d
        shoff, = struct.unpack_from("<I", d, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
        heads = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize)
                 for i in range(shnum)]
        # (address, file offset, size) of every allocated section that has
        # bytes in the file; 8 is SHT_NOBITS.
        self.sections = [(h[3], h[4], h[5]) for h in heads
                         if h[1] != 8 and h[3] != 0]
        self.symbols = []
        for h in heads:
            if h[1] != 2:                     # SHT_SYMTAB
                continue
            strtab = heads[h[6]]
            names = d[strtab[4]: strtab[4] + strtab[5]]
            current_file = None
            for off in range(h[4], h[4] + h[5], 16):
                name, value, size, info, _, shndx = struct.unpack_from(
                    "<IIIBBH", d, off)
                end = names.index(b"\0", name)
                text = names[name:end].decode("utf-8", "replace")
                kind, bind = info & 0xF, info >> 4
                if kind == 4:                 # STT_FILE
                    current_file = text
                    continue
                # (address, kind, size, name, file of a local symbol,
                #  absolute)
                self.symbols.append((value, kind, size, text,
                                     current_file if bind == 0 else None,
                                     shndx == 0xFFF1))

    def read(self, addr: int, n: int) -> bytes | None:
        for base, off, size in self.sections:
            if base <= addr and addr + n <= base + size:
                return self.data[off + addr - base: off + addr - base + n]
        return None


STT_OBJECT, STT_FUNC = 1, 2


# ---------------------------------------------------------- the decoders --

# The low @p bits bits of @p v as a signed number: a branch's offset, in
# the panel's decoder and in the IOmcu's.
def sext(v: int, bits: int) -> int:
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


# -------------------------------------------------------------- C source --

def call_args(text: str, start: int) -> list | None:
    """The top-level arguments of the call whose '(' is at @p start."""
    depth, args, cur = 0, [], start + 1
    for i in range(start, len(text)):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append(text[cur:i].strip())
                return args
        elif ch == "," and depth == 1:
            args.append(text[cur:i].strip())
            cur = i + 1
    return None


# -------------------------------------------------------- the call graph --

class Graph:
    """The deepest chain through a call graph, under one rule for
    recursion: a function already on the chain is not entered again.  A
    chain through a cycle so runs round it at most once, and the depth is
    the largest sum of frames over every chain with no function twice.

    That sum does not split at a function inside a cycle: how far a chain
    gets from there depends on which of the cycle's functions are already
    on it.  The graph is cut into its strongly connected components, the
    groups of functions that can each reach the others.  No chain that
    leaves a group comes back to it, so the depth from a function outside
    the group a chain leaves from is the same for every chain, and is kept.
    Inside a group every way through it is tried, which takes time that
    grows with the factorial of a dense group's size: a search that makes
    more than VISIT_CAP visits inside one group fails and names it.

    groups() is iterative.  depth() and _through() recurse, two Python
    frames for every function of a chain; main() raises the recursion
    limit to 20000, and the deepest panel chain is 31 functions."""

    # The visits to functions of one group a search from one entry into it
    # makes before it fails: about 1 s.  The panel's largest group, 10
    # functions in a ring, takes 80.
    VISIT_CAP = 1_000_000

    def __init__(self, funcs: dict, names: dict) -> None:
        self.funcs = funcs
        self.names = names          # address -> name, ROM included
        self.memo = {}              # address -> depth entered from outside
        self.visits = 0             # calls of _through(), every search
        self._group = None          # address -> its component, a frozenset

    def label(self, addr: int) -> str:
        f = self.funcs.get(addr)
        return f.name if f else self.names.get(addr, f"0x{addr:08x}")

    def groups(self) -> dict:
        """address -> the strongly connected component it lies in, read
        once with Tarjan's algorithm."""
        if self._group is not None:
            return self._group
        index, low, stack, on, group = {}, {}, [], set(), {}
        for root in self.funcs:
            if root in index:
                continue
            work = [(root, iter(sorted(self.funcs[root].calls)))]
            index[root] = low[root] = len(index)
            stack.append(root)
            on.add(root)
            while work:
                v, it = work[-1]
                for w in it:
                    if w not in self.funcs:
                        continue
                    if w not in index:
                        index[w] = low[w] = len(index)
                        stack.append(w)
                        on.add(w)
                        work.append((w, iter(sorted(self.funcs[w].calls))))
                        break
                    if w in on:
                        low[v] = min(low[v], index[w])
                else:
                    work.pop()
                    if work:
                        u = work[-1][0]
                        low[u] = min(low[u], low[v])
                    if low[v] == index[v]:
                        members = set()
                        while True:
                            w = stack.pop()
                            on.discard(w)
                            members.add(w)
                            if w == v:
                                break
                        members = frozenset(members)
                        for w in members:
                            group[w] = members
        self._group = group
        return group

    def depth(self, addr: int) -> tuple:
        """(bytes, [(frame, depth from here, address)]) of the deepest
        chain from @p addr."""
        if addr in self.memo:
            return self.memo[addr]
        f = self.funcs.get(addr)
        if f is None:
            return 0, []
        group = self.groups()[addr]
        r = self._through(addr, group, {addr}, [0, addr])
        self.memo[addr] = r
        return r

    def _through(self, addr: int, group: frozenset, on: set,
                 count: list) -> tuple:
        """The deepest chain from @p addr, which lies in @p group, entering
        no function of the group in @p on again; @p count holds the visits
        this entry into the group has made, and the function it entered
        at."""
        count[0] += 1
        self.visits += 1
        if count[0] > self.VISIT_CAP:
            die(f"the search through a cycle of {len(group)} functions "
                f"passed {self.VISIT_CAP} visits from {self.label(count[1])}: "
                + ", ".join(sorted(self.label(a) for a in group)))
        f = self.funcs[addr]
        best, chain = 0, []
        for c in sorted(f.calls):
            if c in on:
                continue
            if c in group:
                on.add(c)
                d, ch = self._through(c, group, on, count)
                on.discard(c)
            else:
                d, ch = self.depth(c)
            if d > best:
                best, chain = d, ch
        return (f.frame + best, [(f.frame, f.frame + best, addr)] + chain)

    def recursion(self, seen: set) -> list:
        """The cycles among @p seen, one line each: the functions of every
        strongly connected component of more than one function, and every
        function that calls itself."""
        group = self.groups()
        out = set()
        for a in seen:
            g = group.get(a)
            if g is not None and (len(g) > 1 or a in self.funcs[a].calls):
                out.add(", ".join(sorted(self.label(b) for b in g)))
        return sorted(out)

    def reach(self, root: int) -> set:
        seen, todo = set(), [root]
        while todo:
            a = todo.pop()
            if a not in seen and a in self.funcs:
                seen.add(a)
                todo.extend(self.funcs[a].calls)
        return seen


# ------------------------------------------------ the tables in the docs --

def doc_rows(text: str, header: str) -> dict | None:
    """{first cell's quoted name: [the numbers of the cells after the
    second]} for the table under @p header; None without the table.  A
    number is written 8,192 in English and 8 192 in German."""
    if header not in text:
        return None
    rows = {}
    for row in text[text.index(header):].split("\n\n", 1)[0].splitlines()[2:]:
        cells = [c.strip() for c in row.strip().strip("|").split("|")]
        name = re.search(r"`([^`]+)`", cells[0])
        numbers = []
        for cell in cells[2:]:
            digits = re.sub(r"[,\s\u00a0\u202f]", "", cell)
            numbers.append(int(digits) if digits.isdigit() else None)
        rows[name.group(1) if name else cells[0]] = numbers
    return rows
