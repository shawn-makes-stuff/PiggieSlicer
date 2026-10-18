"""Evaluate Orca compatible_printers_condition expressions against a machine preset.

Orca (Preset::is_compatible_with_printer) evaluates the condition with
PlaceholderParser::evaluate_boolean_expression against the printer preset's config plus
`printer_preset` (the printer preset name) and `num_extruders` (len(nozzle_diameter)).
The machines of a vendor are known at conversion time, so the converter evaluates the
condition once per machine and turns the result into an ordinary 3.0 condition.

Supported grammar (the subset of Orca's macro language boolean expressions):
  or/||, and/&&, not/!, == != < > <= >= =~ !~, + - * /, parentheses, numbers, "strings",
  /regex/ (full match, like boost::regex_match), identifiers with an optional [index],
  true/false, one_of(x, a, b, ...).
Anything else raises ConditionError, and the converter reports the preset.
"""
import re


class ConditionError(Exception):
    pass


_TOKEN = re.compile(r"""
    (?P<ws>\s+)|
    (?P<num>\d+\.\d*|\.\d+|\d+)|
    (?P<str>"(?:[^"\\]|\\.)*")|
    (?P<op>\|\||&&|==|!=|<=|>=|=~|!~|<>|[<>!()\[\],+\-*/])|
    (?P<id>[A-Za-z_][A-Za-z0-9_]*)
""", re.X)


def _tokens(text):
    out, i = [], 0
    while i < len(text):
        if text[i] == "/" and out and out[-1] in (("op", "=~"), ("op", "!~")):
            j = i + 1
            while j < len(text) and text[j] != "/":
                j += 2 if text[j] == "\\" else 1
            out.append(("re", text[i + 1:j]))
            i = j + 1
            continue
        m = _TOKEN.match(text, i)
        if not m:
            raise ConditionError(f"cannot tokenize at {text[i:i + 20]!r}")
        i = m.end()
        kind = m.lastgroup
        if kind == "ws":
            continue
        val = m.group(kind)
        if kind == "id" and val in ("and", "or", "not"):
            kind = "op"
        out.append((kind, val))
    return out


def _num(v):
    if isinstance(v, bool):
        return float(v)
    if isinstance(v, (int, float)):
        return float(v)
    s = str(v).strip().rstrip("%")
    return float(s)


class _Parser:
    def __init__(self, text, lookup):
        self.t = _tokens(text)
        self.i = 0
        self.lookup = lookup

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else (None, None)

    def take(self, val=None):
        tok = self.peek()
        if val is not None and tok[1] != val:
            raise ConditionError(f"expected {val!r}, got {tok[1]!r}")
        self.i += 1
        return tok

    def parse(self):
        v = self.or_()
        if self.i != len(self.t):
            raise ConditionError(f"unexpected {self.peek()[1]!r}")
        return bool(v)

    def or_(self):
        v = self.and_()
        while self.peek()[1] in ("or", "||"):
            self.take()
            r = self.and_()
            v = bool(v) or bool(r)
        return v

    def and_(self):
        v = self.not_()
        while self.peek()[1] in ("and", "&&"):
            self.take()
            r = self.not_()
            v = bool(v) and bool(r)
        return v

    def not_(self):
        if self.peek()[1] in ("not", "!"):
            self.take()
            return not bool(self.not_())
        return self.cmp()

    def cmp(self):
        left = self.add()
        op = self.peek()[1]
        if op in ("=~", "!~"):
            self.take()
            kind, rx = self.take()
            if kind != "re":
                raise ConditionError("regex expected")
            hit = re.fullmatch(rx, str(left), re.S) is not None
            return hit if op == "=~" else not hit
        if op in ("==", "!=", "<>", "<", ">", "<=", ">="):
            self.take()
            right = self.add()
            if op in ("==", "!=", "<>"):
                try:
                    eq = abs(_num(left) - _num(right)) < 1e-9
                except (ValueError, TypeError):
                    eq = str(left) == str(right)
                return eq if op == "==" else not eq
            a, b = _num(left), _num(right)
            return {"<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b}[op]
        return left

    def add(self):
        v = self.mul()
        while self.peek()[1] in ("+", "-"):
            op = self.take()[1]
            r = self.mul()
            v = _num(v) + _num(r) if op == "+" else _num(v) - _num(r)
        return v

    def mul(self):
        v = self.unary()
        while self.peek()[1] in ("*", "/"):
            op = self.take()[1]
            r = self.unary()
            v = _num(v) * _num(r) if op == "*" else _num(v) / _num(r)
        return v

    def unary(self):
        if self.peek()[1] == "-":
            self.take()
            return -_num(self.unary())
        return self.primary()

    def primary(self):
        kind, val = self.take()
        if kind == "num":
            return float(val)
        if kind == "str":
            return bytes(val[1:-1], "utf-8").decode("unicode_escape")
        if kind == "op" and val == "(":
            v = self.or_()
            self.take(")")
            return v
        if kind == "id":
            if val == "true":
                return True
            if val == "false":
                return False
            if val == "one_of" and self.peek()[1] == "(":
                self.take("(")
                x = self.add()
                hits = False
                while self.peek()[1] == ",":
                    self.take()
                    if self.peek()[0] == "re":
                        hits |= re.fullmatch(self.take()[1], str(x), re.S) is not None
                    else:
                        y = self.add()
                        hits |= str(x) == str(y)
                self.take(")")
                return hits
            idx = None
            if self.peek()[1] == "[":
                self.take("[")
                idx = int(_num(self.add()))
                self.take("]")
            return self.lookup(val, idx)
        raise ConditionError(f"unexpected {val!r}")


def evaluate(condition, machine_name, machine_cfg):
    def lookup(name, idx):
        if name == "printer_preset":
            v = machine_name
        elif name == "num_extruders":
            v = len(machine_cfg.get("nozzle_diameter", [""])) if isinstance(machine_cfg.get("nozzle_diameter"), list) else 1
        elif name in machine_cfg:
            v = machine_cfg[name]
        else:
            raise ConditionError(f"unknown variable {name!r}")
        if isinstance(v, list):
            if idx is None:
                idx = 0
            if idx >= len(v):
                raise ConditionError(f"{name}[{idx}] out of range")
            v = v[idx]
        return v
    return _Parser(condition, lookup).parse()
