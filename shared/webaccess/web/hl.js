// The highlighter of the panel's text viewer, for the Wi-Fi page: the rules
// of shared/files/text_hl.c on a line's UTF-8 bytes, so a line gets the
// spans the panel draws it in.  The class names, the unit table, the
// verdict words, the label's cells and the span cap come from the page's
// data (wa_app.c writes them from the C tables); the rules are written
// twice, here and in text_hl.c, and test/host/check_wa_hl.py holds the two
// to the same spans on every fixture line test_wa_hl writes.
//
// Also the viewer's line split (text_buf.h): a line ends at LF, CR LF or
// a lone CR, a line end at the file's end starts no line of its own, and a
// leading UTF-8 BOM (byte order mark) is no part of line 1.
//
// SPDX-License-Identifier: MIT

var RcbHl = (function () {
  'use strict';

  // The kinds of text_hl_kind_t; the numbers are stable.
  var KIND_TEXT = 0;
  var KIND_JSON = 1;
  var KIND_CSV = 2;
  var KIND_PLAIN = 3;
  // Characters a heading has at least, digits of a checksum (text_hl.c).
  var HEADING_MIN = 3;
  var CHECKSUM_DIGITS = 8;
  var LETTER_NONE = 0;
  var LETTER_UPPER = 1;
  var LETTER_LOWER = 2;

  function isWord(b) {
    return (b >= 48 && b <= 57) || (b >= 65 && b <= 90) ||
      (b >= 97 && b <= 122) || b === 95 || b >= 0x80;
  }

  function isDigit(b) {
    return b >= 48 && b <= 57;
  }

  function isHex(b) {
    return isDigit(b) || (b >= 65 && b <= 70) || (b >= 97 && b <= 102);
  }

  function isBlank(b) {
    return b === 32 || b === 9;
  }

  function isAlpha(b) {
    return (b >= 65 && b <= 90) || (b >= 97 && b <= 122);
  }

  // The tables of the page's data, with each word and unit as its bytes.
  function tables(data) {
    var enc = new TextEncoder();
    var t = { c: {}, units: [], lists: [[], [], []], cap: data.cap,
      label: data.label };
    data.cls.forEach(function (name, i) { t.c[name] = i; });
    data.units.forEach(function (u) {
      t.units.push({ b: enc.encode(u[0]), c: u[1] });
    });
    [data.ok, data.fail, data.warn].forEach(function (list, k) {
      list.forEach(function (w) { t.lists[k].push(enc.encode(w)); });
    });
    t.wordCls = [t.c.ok, t.c.fl, t.c.wn];
    return t;
  }

  // The spans of a line: [start, length, class] each.
  function Spans(cap, text) {
    this.out = [];
    this.cap = cap;
    this.full = false;
    this.text = text;
  }

  Spans.prototype.emit = function (start, len, cls) {
    var n = this.out.length;
    if (len === 0) {
      return;
    }
    if (n > 0 && (this.full || this.out[n - 1][2] === cls)) {
      this.out[n - 1][1] += len;
      return;
    }
    if (n + 1 === this.cap) {
      // The last slot: the rest of the line is one TEXT span.
      cls = this.text;
      this.full = true;
    }
    this.out.push([start, len, cls]);
  };

  function same(s, at, w) {
    for (var k = 0; k < w.length; ++k) {
      if (s[at + k] !== w[k]) {
        return false;
      }
    }
    return true;
  }

  function apart(e, nb) {
    return isWord(e) ? !isWord(nb) : nb !== e;
  }

  // The longest word of list at byte i; 0 for none.
  function wordAt(s, len, i, list) {
    var best = 0;
    for (var k = 0; k < list.length; ++k) {
      var w = list[k];
      var wl = w.length;
      if (wl === 0 || wl <= best || wl > len - i || !same(s, i, w)) {
        continue;
      }
      if ((i > 0 && !apart(w[0], s[i - 1])) ||
          (i + wl < len && !apart(w[wl - 1], s[i + wl]))) {
        continue;
      }
      best = wl;
    }
    return best;
  }

  function unitClass(t, s, a, b) {
    for (var k = 0; k < t.units.length; ++k) {
      var u = t.units[k].b;
      if (u.length === b - a && same(s, a, u)) {
        return t.units[k].c;
      }
    }
    return t.c.tx;
  }

  function unitEnd(s, len, k) {
    var u = k;
    if (u < len && s[u] === 37) {
      return u + 1;
    }
    if (u < len && s[u] === 47) {
      u++;
    }
    while (u < len) {
      if (isAlpha(s[u])) {
        u++;
      } else if (u + 1 < len && s[u] === 0xCE && s[u + 1] === 0xA9) {
        u += 2;
      } else {
        break;
      }
    }
    return u;
  }

  function unitClassAt(t, s, len, k, u) {
    if (u === k || (u < len && isWord(s[u]))) {
      return t.c.tx;
    }
    return unitClass(t, s, k, u);
  }

  // A number at byte i with its unit: [bytes, class]; [0, TEXT] for none.
  function numberAt(t, s, len, i) {
    var j = i;
    var tx = t.c.tx;
    if (i > 0 && (isWord(s[i - 1]) || s[i - 1] === 46)) {
      return [0, tx];
    }
    if ((s[j] === 43 || s[j] === 45) && j + 1 < len) {
      j++;
    }
    if (!isDigit(s[j])) {
      return [0, tx];
    }
    while (j < len && isDigit(s[j])) {
      j++;
    }
    if (j + 1 < len && s[j] === 46 && isDigit(s[j + 1])) {
      j++;
      while (j < len && isDigit(s[j])) {
        j++;
      }
    }
    if (j + 1 < len && s[j] === 46 && isDigit(s[j + 1])) {
      return [0, tx];
    }
    var c;
    var u;
    if (j < len && isWord(s[j])) {
      u = unitEnd(s, len, j);
      c = unitClassAt(t, s, len, j, u);
      return (c !== tx) ? [u - i, c] : [0, tx];
    }
    var k = (j < len && s[j] === 32) ? j + 1 : j;
    u = unitEnd(s, len, k);
    c = unitClassAt(t, s, len, k, u);
    return (c !== tx) ? [u - i, c] : [j - i, tx];
  }

  function hexLine(s, len) {
    var i = 0;
    while (len > 0 && isBlank(s[len - 1])) {
      len--;
    }
    if (len >= 6 && isHex(s[0]) && isHex(s[1]) && s[2] === 58) {
      for (i = 3; i + 3 <= len; i += 3) {
        if (s[i] !== 32 || !isHex(s[i + 1]) || !isHex(s[i + 2])) {
          return false;
        }
      }
      return i === len;
    }
    if (len > 8 && same(s, 0, [99, 104, 101, 99, 107, 115, 117, 109])) {
      i = 8;
      while (i < len && isBlank(s[i])) {
        i++;
      }
      if (i === 8 || len - i !== CHECKSUM_DIGITS) {
        return false;
      }
      while (i < len && isHex(s[i])) {
        i++;
      }
      return i === len;
    }
    while (i < len && s[i] >= 97 && s[i] <= 122) {
      i++;
    }
    if (i === 0 || i + 1 >= len || s[i] !== 61) {
      return false;
    }
    for (i++; i < len; ++i) {
      if (!isHex(s[i])) {
        return false;
      }
    }
    return true;
  }

  // The case of the letter at s[i] and its bytes: ASCII and the 7 German
  // letters.
  function letter(s, i, n) {
    var b = s[i];
    if (b >= 65 && b <= 90) {
      return [LETTER_UPPER, 1];
    }
    if (b >= 97 && b <= 122) {
      return [LETTER_LOWER, 1];
    }
    if (b === 0xC3 && n >= 2) {
      var c = s[i + 1];
      if (c === 0x84 || c === 0x96 || c === 0x9C) {
        return [LETTER_UPPER, 2];
      }
      if (c === 0xA4 || c === 0xB6 || c === 0xBC || c === 0x9F) {
        return [LETTER_LOWER, 2];
      }
    }
    return [LETTER_NONE, 1];
  }

  function heading(s, len) {
    var chars = 0;
    var open = false;
    var n = 1;
    if (len === 0 || letter(s, 0, len)[0] !== LETTER_UPPER) {
      return false;
    }
    for (var i = 0; i < len; i += n) {
      n = 1;
      if (s[i] === 40) {
        open = true;
      }
      if (!open) {
        var l = letter(s, i, len - i);
        n = l[1];
        if (l[0] === LETTER_LOWER) {
          return false;
        }
      }
      if ((s[i] & 0xC0) !== 0x80) {
        chars++;
      }
    }
    return chars >= HEADING_MIN;
  }

  function labelEnd(t, s, len) {
    var cells = 0;
    if (len === 0 || isBlank(s[0])) {
      return 0;
    }
    for (var i = 0; i < len && cells < t.label; ++i) {
      if ((s[i] & 0xC0) === 0x80) {
        continue;
      }
      if (s[i] === 58 || s[i] === 61) {
        return (i + 1 === len || isBlank(s[i + 1])) ? i + 1 : 0;
      }
      cells++;
    }
    return 0;
  }

  // Comments, verdict words and numbers from byte i on.
  function rest(t, sp, s, len, i) {
    var tx = t.c.tx;
    var text = i;
    while (i < len) {
      if (s[i] === 35 && (i === 0 || isBlank(s[i - 1]))) {
        sp.emit(text, i - text, tx);
        sp.emit(i, len - i, t.c.cm);
        return;
      }
      var best = 0;
      var cls = tx;
      for (var k = 0; k < 3; ++k) {
        var wl = wordAt(s, len, i, t.lists[k]);
        if (wl > best) {
          best = wl;
          cls = t.wordCls[k];
        }
      }
      if (best === 0) {
        var r = numberAt(t, s, len, i);
        best = r[0];
        cls = r[1];
      }
      if (best > 0 && cls !== tx) {
        sp.emit(text, i - text, tx);
        sp.emit(i, best, cls);
        i += best;
        text = i;
      } else {
        i += (best > 0) ? best : 1;
      }
    }
    sp.emit(text, len - text, tx);
  }

  function textLine(t, sp, s, len, lineNo) {
    var lead = 0;
    if (hexLine(s, len)) {
      sp.emit(0, len, t.c.hx);
      return;
    }
    while (lead < len && isBlank(s[lead])) {
      lead++;
    }
    if (lead < len && s[lead] === 35) {
      sp.emit(0, lead, t.c.tx);
      sp.emit(lead, len - lead, t.c.cm);
      return;
    }
    var label = labelEnd(t, s, len);
    if (lineNo === 0 || (label === 0 && heading(s, len))) {
      sp.emit(0, len, t.c.hd);
      return;
    }
    sp.emit(0, label, t.c.lb);
    rest(t, sp, s, len, label);
  }

  function jsonNumber(s, len, i) {
    var j = i;
    if (s[j] === 45) {
      j++;
    }
    if (j >= len || !isDigit(s[j])) {
      return 0;
    }
    while (j < len && isDigit(s[j])) {
      j++;
    }
    if (j + 1 < len && s[j] === 46 && isDigit(s[j + 1])) {
      j++;
      while (j < len && isDigit(s[j])) {
        j++;
      }
    }
    if (j < len && (s[j] === 101 || s[j] === 69)) {
      var k = j + 1;
      if (k < len && (s[k] === 43 || s[k] === 45)) {
        k++;
      }
      if (k < len && isDigit(s[k])) {
        j = k;
        while (j < len && isDigit(s[j])) {
          j++;
        }
      }
    }
    return j - i;
  }

  // true, false, null as bytes.
  var LITERALS = [[116, 114, 117, 101], [102, 97, 108, 115, 101],
    [110, 117, 108, 108]];
  // { } [ ] , :
  var MARKS = [123, 125, 91, 93, 44, 58];

  function jsonLine(t, sp, s, len) {
    var i = 0;
    while (i < len) {
      var b = s[i];
      var n = 1;
      var cls = t.c.tx;
      var m;
      if (MARKS.indexOf(b) >= 0) {
        cls = t.c.pu;
      } else if (b === 34) {
        var j = i + 1;
        while (j < len && s[j] !== 34) {
          j += (s[j] === 92) ? 2 : 1;
        }
        j = (j < len) ? j + 1 : len;
        n = j - i;
        while (j < len && isBlank(s[j])) {
          j++;
        }
        cls = (j < len && s[j] === 58) ? t.c.ky : t.c.tx;
      } else if ((m = jsonNumber(s, len, i)) > 0) {
        n = m;
        cls = t.c.nm;
      } else {
        for (var k = 0; k < LITERALS.length; ++k) {
          var w = LITERALS[k];
          var wl = w.length;
          if (wl <= len - i && same(s, i, w) &&
              (i === 0 || !isWord(s[i - 1])) &&
              (i + wl === len || !isWord(s[i + wl]))) {
            n = wl;
            cls = t.c.lt;
          }
        }
      }
      sp.emit(i, n, cls);
      i += n;
    }
  }

  function csvLine(t, sp, s, len, lineNo) {
    var delim = (s.indexOf(59) >= 0) ? 59 : 44;
    var cell = (lineNo === 0) ? t.c.ch : t.c.tx;
    for (var i = 0; i < len; ++i) {
      sp.emit(i, 1, (s[i] === delim) ? t.c.pu : cell);
    }
  }

  // The spans of one line, a Uint8Array without its line end, as
  // text_hl_line() gives them with room for t.cap spans.
  function line(t, s, lineNo, kind) {
    var sp = new Spans(t.cap, t.c.tx);
    var len = s.length;
    if (t.cap === 0 || len === 0) {
      return sp.out;
    }
    if (kind === KIND_TEXT) {
      textLine(t, sp, s, len, lineNo);
    } else if (kind === KIND_JSON) {
      jsonLine(t, sp, s, len);
    } else if (kind === KIND_CSV) {
      csvLine(t, sp, s, len, lineNo);
    } else {
      sp.emit(0, len, t.c.tx);
    }
    return sp.out;
  }

  function lowerAscii(c) {
    return (c >= 'A' && c <= 'Z') ? String.fromCharCode(c.charCodeAt(0) + 32)
      : c;
  }

  function sameExt(name, e, n, ext) {
    if (n - e !== ext.length) {
      return false;
    }
    for (var i = 0; i < ext.length; ++i) {
      if (lowerAscii(name.charAt(e + i)) !== ext.charAt(i)) {
        return false;
      }
    }
    return true;
  }

  function extAt(name, n) {
    var i = n;
    while (i > 0 && name.charAt(i - 1) !== '.' && name.charAt(i - 1) !== '/') {
      i--;
    }
    return (i > 0 && name.charAt(i - 1) === '.') ? i - 1 : n;
  }

  // How a file is classed by its name (text_hl_kind_of()).
  function kindOf(name) {
    if (typeof name !== 'string') {
      return KIND_PLAIN;
    }
    var n = name.length;
    var e = extAt(name, n);
    if (sameExt(name, e, n, '.new')) {
      n = e;
      e = extAt(name, n);
    }
    if (sameExt(name, e, n, '.txt')) {
      return KIND_TEXT;
    }
    if (sameExt(name, e, n, '.json')) {
      return KIND_JSON;
    }
    if (sameExt(name, e, n, '.csv')) {
      return KIND_CSV;
    }
    return KIND_PLAIN;
  }

  // The lines of a file's bytes as the viewer shows them: {s, e} with the
  // offset of each line's first byte and of the byte after its last.
  function lines(b) {
    var s = [];
    var e = [];
    var at = (b.length >= 3 && b[0] === 0xEF && b[1] === 0xBB &&
      b[2] === 0xBF) ? 3 : 0;
    var start = at;
    for (var i = at; i < b.length; ++i) {
      if (b[i] === 10 || b[i] === 13) {
        s.push(start);
        e.push(i);
        if (b[i] === 13 && i + 1 < b.length && b[i + 1] === 10) {
          i++;
        }
        start = i + 1;
      }
    }
    if (start < b.length) {
      s.push(start);
      e.push(b.length);
    }
    return { s: s, e: e };
  }

  return { tables: tables, line: line, kindOf: kindOf, lines: lines };
}());

if (typeof module !== 'undefined') {
  module.exports = RcbHl;
}
