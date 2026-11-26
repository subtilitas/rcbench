"""The printer file reader's lines of templates, answers, steps and jobs.

The mixin StepLines holds the methods of prn_parse_ext.c in Python: tmpl,
answer, field, error, step and its lines, job, abort and verdict.
printer_files.Reader takes them; it holds the state they read (the line's
tokens, the keys seen, the open step).

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

from printer_spec import (
    C_CHECK, C_CHUNK, C_ROWS, C_STEP, C_TMPL, CK_SVPAGE, FIELDS,
    FKINDS, OP_CLEAR, OP_EQ_N, OP_EQ_V, OP_EQ_V1, OP_NE_N, OP_NE_V, OP_SET,
    RELINK_VERDICT, S_DATA, S_SEND, S_WAIT, T_BYTE, T_CK, T_DATA,
    TOKENS_POOL, V_C0, VARS, Refused, code_tok, fixed_len, hexbytes,
    name_tok, new_send, new_step, num_text, placeholder, ranges_ok, var_in,
    word,
)


class StepLines:
    """The lines of prn_parse_ext.c, as a mixin of printer_files.Reader."""

    def tokens(self, start: int, ctx: int, most: int) -> tuple[int, int]:
        """A template's tokens from token @p start on.

        Args:
            start: the first token.
            ctx: a C_* context.
            most: tokens at most.

        Returns:
            The run: its first index in the pool and its count.

        Raises:
            Refused: E07, E10, E11, E22, E30.
        """
        s = self.s
        count = self.nt - start
        if count > most:
            raise Refused(7 if ctx == C_CHECK else 10)
        if len(s.tok) + count > TOKENS_POOL:
            raise Refused(30)
        out = []
        data = svpage = 0
        for i in range(count):
            t = self.t(start + i)
            b = hexbytes(t, 1) if len(t) == 2 else None
            if b is not None and ctx != C_CHECK:
                tok = (T_BYTE, b[0], 0, 0)
            elif len(t) >= 2 and t[0] == "{" and t[-1] == "}":
                tok = placeholder(t[1:-1], ctx)
            else:
                raise Refused(11)
            data += tok[0] == T_DATA
            svpage += tok[0] == T_CK and tok[1] == CK_SVPAGE
            if ctx == C_CHECK and tok[0] != T_CK:
                raise Refused(11)
            out.append(tok)
        if ctx in (C_CHUNK, C_ROWS) and data != 1:
            raise Refused(22)
        if svpage > 1:
            raise Refused(11)
        first = len(s.tok)
        s.tok.extend(out)
        return (first, count)

    def tmpl_line(self) -> None:
        """A tmpl line."""
        s = self.s
        if self.nt < 3:
            raise Refused(7)
        if len(s.tmpl) >= 16:
            raise Refused(10)
        name = name_tok(self.t(1))
        if self.tmpl_find(1) >= 0:
            raise Refused(8)
        seq = self.tokens(2, C_TMPL, 40)
        n = fixed_len(s, seq)
        if not ranges_ok(s, seq, n, n):
            raise Refused(12)
        s.tmpl.append((name, seq))

    def tmpl_find(self, i: int) -> int:
        """A template's index.

        Args:
            i: the token naming it.

        Returns:
            The index, or -1.
        """
        for k, (name, _) in enumerate(self.s.tmpl):
            if self.is_(i, name):
                return k
        return -1

    def step_find(self, name: str) -> int:
        """A step's index.

        Args:
            name: its name.

        Returns:
            The index, or -1.
        """
        for k, st in enumerate(self.s.step):
            if st["name"] == name:
                return k
        return -1

    def args(self, start: int, stop: int, snd: dict) -> None:
        """The arguments of a send.

        Args:
            start: the first token.
            stop: past the last.
            snd: the send.

        Raises:
            Refused: E07, E14.
        """
        given = 0
        for i in range(start, stop):
            t = self.t(i)
            if "=" not in t:
                raise Refused(7)
            name, val = t.split("=", 1)
            a = word(name, ["a", "p0", "p1", "p2"])
            if a < 0 or given & (1 << a):
                raise Refused(7)
            given |= 1 << a
            try:
                snd["n"][a] = num_text(val, 0, 0xFFFFFFFF)
                continue
            except Refused:
                pass
            var = word(val, VARS) + 1
            if not var_in(var, C_STEP):
                raise Refused(14)
            snd["var"][a] = var

    def send_of(self, start: int, stop: int, snd: dict) -> None:
        """A send: a template and its arguments.

        Args:
            start: the token naming the template.
            stop: past the last argument.
            snd: the send.

        Raises:
            Refused: E07, E14.
        """
        k = self.tmpl_find(start)
        if k < 0:
            raise Refused(14)
        snd["tmpl"] = k
        self.args(start + 1, stop, snd)

    # -- answers --------------------------------------------------------

    def answer_line(self) -> None:
        """An answer, field or error line."""
        s = self.s
        if self.is_(0, "answer"):
            if self.nt != 3:
                raise Refused(7)
            if not self.has("rxkey"):
                raise Refused(9)
            if len(s.answer) >= 8:
                raise Refused(28)
            key = hexbytes(self.t(1), 1)
            if key is None:
                raise Refused(7)
            if any(k == key[0] for k, _ in s.answer):
                raise Refused(8)
            s.answer.append((key[0], self.num(2, s.key_off + 1, s.rx_max)))
            return
        if self.is_(0, "field"):
            if self.nt not in (5, 6):
                raise Refused(7)
            if len(s.field) >= 24:
                raise Refused(28)
            key = hexbytes(self.t(1), 1)
            if key is None:
                raise Refused(7)
            mins = [m for k, m in s.answer if k == key[0]]
            if not mins:
                raise Refused(14)
            name = word(self.t(2), FIELDS)
            if name < 0:
                raise Refused(7)
            if s.field_find(key[0], name) >= 0:
                raise Refused(8)
            kind = word(self.t(4), FKINDS)
            has_n = kind in (0, 6)
            if kind < 0 or has_n != (self.nt == 6):
                raise Refused(7)
            off = self.num(3, 0, 127)
            n = 0
            if has_n:
                n = self.num(5, 0 if kind == 0 else 1, 7 if kind == 0 else 8)
            w = n if kind == 6 else (1, 1, 2, 2, 4, 4)[kind]
            if off + w > mins[-1]:
                raise Refused(13)
            s.field.append((key[0], name, off, kind, n))
            return
        if self.nt != 3:
            raise Refused(7)
        if len(s.error) >= 12:
            raise Refused(28)
        name = word(self.t(1), FIELDS)
        if name < 0 or not any(f[1] == name and f[3] == 0 for f in s.field):
            raise Refused(14)
        s.error.append((name, code_tok(self.t(2))))

    # -- steps ----------------------------------------------------------

    def step_field(self, i: int) -> int:
        """A field of the open step's answer.

        Args:
            i: the token.

        Returns:
            The field's index in FIELDS.

        Raises:
            Refused: E14.
        """
        st = self.s.step[-1]
        name = word(self.t(i), FIELDS)
        if name < 0 or not st["has_expect"] \
                or self.s.field_find(st["expect"], name) < 0:
            raise Refused(14)
        return name

    def cond(self, start: int, stop: int, err: int = 0) -> tuple:
        """A condition.

        Args:
            start: the first token.
            stop: past the last.
            err: its end code, for a check.

        Returns:
            (field, op, n, var, err).

        Raises:
            Refused: E07, E14.
        """
        n = stop - start
        if n not in (2, 3):
            raise Refused(7)
        field = self.step_field(start)
        if n == 2:
            if self.t(start + 1) not in ("set", "clear"):
                raise Refused(7)
            op = OP_SET if self.is_(start + 1, "set") else OP_CLEAR
            return (field, op, 0, 0, err)
        eq = self.is_(start + 1, "=")
        if not eq and not self.is_(start + 1, "!="):
            raise Refused(7)
        try:
            v = num_text(self.t(start + 2), 0, 0xFFFFFFFF)
            return (field, OP_EQ_N if eq else OP_NE_N, v, 0, err)
        except Refused:
            pass
        t = self.t(start + 2)
        minus1 = len(t) > 2 and t.endswith("-1")
        if minus1:
            t = t[:-2]
        var = word(t, VARS) + 1
        if not var_in(var, C_STEP):
            raise Refused(14)
        if minus1 and not eq:
            raise Refused(7)
        op = OP_EQ_V1 if minus1 else (OP_EQ_V if eq else OP_NE_V)
        return (field, op, 0, var, err)

    def sub_once(self, sub: str, nt: int | None = None) -> None:
        """Mark a sub-key seen in the open step.

        Args:
            sub: the sub-key.
            nt: the line's token count, or None.

        Raises:
            Refused: E08, then E07.
        """
        if sub in self.sub:
            raise Refused(8)
        self.sub.add(sub)
        if nt is not None and self.nt != nt:
            raise Refused(7)

    def step_line(self) -> None:
        """A line of the open step."""
        s = self.s
        st = s.step[-1]
        k = self.t(0)
        nums = {"wait": (1, 5000), "poll": (20, 5000), "within": (100, 60000),
                "delay": (1, 5000), "tries": (1, 5)}
        if k == "send":
            self.sub_once("send")
            if self.nt < 2:
                raise Refused(7)
            st["kind"] = S_SEND
            self.send_of(1, self.nt, st["send"])
        elif k in ("data", "flags", "roll"):
            self.sub_once(k, 2 if k == "flags" else 1)
            if k == "data":
                st["kind"] = S_DATA
            elif k == "flags":
                st["flags"] = True
                if not self.is_(1, "on"):
                    raise Refused(7)
            else:
                if not st["has_expect"] \
                        or s.field_find(st["expect"], 11) < 0 \
                        or s.field_find(st["expect"], 12) < 0:
                    raise Refused(14)
                st["roll"] = True
        elif k in nums:
            if k == "wait":
                st["kind"] = S_WAIT
            self.sub_once(k, 2)
            st[k] = self.num(1, *nums[k])
        elif k == "expect":
            self.sub_once("expect", 2)
            st["has_expect"] = True
            b = hexbytes(self.t(1), 1)
            if b is None:
                raise Refused(7)
            st["expect"] = b[0]
        elif k == "else":
            self.sub_once("else", 2)
            st["else"] = code_tok(self.t(1))
        elif k == "until":
            self.sub_once("until")
            if self.nt < 3:
                raise Refused(7)
            j = 2
            while j < self.nt and not self.is_(j, "and"):
                j += 1
            st["until"] = [self.cond(1, j)]
            if j < self.nt:
                st["until"].append(self.cond(j + 1, self.nt))
        elif k == "save":
            if len(st["save"]) >= 4:
                raise Refused(30)
            if self.nt != 3:
                raise Refused(7)
            var = word(self.t(1), VARS) + 1
            if var < V_C0:
                raise Refused(7)
            st["save"].append((var, self.step_field(2)))
        elif k == "check":
            if len(st["check"]) >= 4:
                raise Refused(30)
            j = 1
            while j < self.nt and not self.is_(j, "else"):
                j += 1
            if j + 2 != self.nt:
                raise Refused(7)
            c = self.cond(1, j)
            st["check"].append(c[:4] + (code_tok(self.t(j + 1)),))
        elif k == "relink":
            self.sub_once("relink", 2)
            self.relink_line[len(s.step) - 1] = self.lineno
            if self.is_(1, "verdict"):
                st["relink"] = RELINK_VERDICT
            st["relink_name"] = name_tok(self.t(1))
        else:
            raise Refused(6)

    def close_step(self) -> None:
        """Close the open step: its lines must fit together (E24)."""
        st = self.s.step[-1]
        f = self.sub
        kinds = sum(1 for k in ("send", "data", "wait") if k in f)
        ok = kinds == 1
        answer = {"expect", "poll", "until", "tries", "roll", "delay",
                  "flags"}
        if "wait" in f:
            ok = ok and not f & (answer | {"within", "else"}) \
                and not st["save"] and not st["check"]
        elif "data" in f:
            ok = ok and "within" in f and not f & answer \
                and not st["save"] and not st["check"]
        else:
            ok = ok and "within" in f and (("poll" in f) == ("until" in f)) \
                and not ("poll" in f and ("expect" not in f or "tries" in f)) \
                and not ("flags" in f and "expect" not in f)
        self.in_step = False
        if not ok:
            self.lineno = self.step_lineno
            raise Refused(24)

    # -- jobs, abort, verdict -------------------------------------------

    def job(self) -> None:
        """A job line."""
        s = self.s
        printing = self.is_(1, "print")
        if not printing and not self.is_(1, "query"):
            raise Refused(6)
        self.once("print" if printing else "query")
        n = self.nt - 2
        if n == 0 or n > 24:
            raise Refused(15)
        out: list[int] = []
        data = 0
        for i in range(n):
            k = self.step_find(self.t(2 + i))
            if k < 0 or k in out:
                raise Refused(15)
            out.append(k)
            data += s.step[k]["kind"] == S_DATA
        if data != (1 if printing else 0):
            raise Refused(15)
        if printing:
            s.print = out
        else:
            s.query = out

    def print_pos(self, i: int) -> int:
        """A step's position in the print job.

        Args:
            i: the token naming it.

        Returns:
            The position, or -1.
        """
        k = self.step_find(self.t(i))
        return self.s.print.index(k) if k >= 0 and k in self.s.print else -1

    def abort_line(self) -> None:
        """The abort line."""
        s = self.s
        self.once_n("abort", 6, True)
        k = 2
        while k < self.nt and not self.is_(k, "from"):
            k += 1
        if k + 4 != self.nt or not self.is_(k + 2, "to"):
            raise Refused(7)
        if not self.has("print"):
            raise Refused(26)
        snd = new_send()
        self.send_of(1, k, snd)
        a, b = self.print_pos(k + 1), self.print_pos(k + 3)
        if a < 0 or b < a:
            raise Refused(26)
        s.abort, s.abort_from, s.abort_to = snd, a, b

    def verdict(self) -> None:
        """The verdict line."""
        s = self.s
        self.once_n("verdict", 2, True)
        kind = word(self.t(1), ["counter", "result", "done", "none"])
        if kind < 0:
            raise Refused(7)
        s.verdict = kind
        if kind >= 2:
            if self.nt != 2:
                raise Refused(7)
            return
        if self.nt < (6 if kind == 0 else 4) or (kind == 1 and self.nt > 7):
            raise Refused(7)
        name = word(self.t(2), FIELDS)
        f = s.field_any(name) if name >= 0 else -1
        if f < 0:
            raise Refused(14)
        s.vd_field, s.vd_key = name, s.field[f][0]
        if kind == 1:
            s.result = [self.num(i, 0, 0xFFFFFFFF) for i in range(3, self.nt)]
            return
        s.vd_reads = self.num(3, 1, 10)
        s.vd_gap_ms = self.num(4, 0, 5000)
        self.send_of(5, self.nt, s.vd_send)

    def ext(self) -> bool:
        """The keys of prn_parse_ext.c.

        Returns:
            False for another key.
        """
        s = self.s
        if self.in_step:
            self.step_line()
            return True
        k = self.t(0)
        if k == "tmpl":
            self.tmpl_line()
        elif k in ("answer", "field", "error"):
            self.answer_line()
        elif k == "job":
            self.job()
        elif k == "abort":
            self.abort_line()
        elif k == "verdict":
            self.verdict()
        elif k == "step":
            if self.nt != 2:
                raise Refused(7)
            if len(s.step) >= 24:
                raise Refused(28)
            name = name_tok(self.t(1))
            if self.step_find(name) >= 0:
                raise Refused(8)
            s.step.append(new_step(name))
            self.in_step = True
            self.step_lineno = self.lineno
            self.sub = set()
        else:
            return False
        return True
