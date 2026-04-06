#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
html_to_header.py
=================
Convert an HTML file into a gzip-compressed C header (xxd -i style).

Pipeline
--------
    Original HTML  ->  Minified HTML  ->  JS-Obfuscated HTML  ->  Gzip  ->  C Header

Sizes at every stage are recorded inside the generated header as comments.

Usage
-----
    python3 html_to_header.py index.html [--no-minify] [--no-obfuscate]

Flags
-----
    --no-minify     Skip HTML/CSS/JS minification step.
    --no-obfuscate  Skip JavaScript obfuscation step.

Output
------
    Saves the generated header into the 'src' folder relative to where
    this script lives (e.g. utils/  ->  src/page_index.h).

Requirements
------------
    pip install minify-html jsmin  (optional but recommended)
    Works with Python 3.10+
"""

import sys
import os
import gzip
import re
import argparse
from datetime import datetime

# ──────────────────────────────────────────────────────────────────────────────
# Optional dependency helpers
# ──────────────────────────────────────────────────────────────────────────────

def _try_minify_html(source: str) -> str:
    """Minify HTML using minify-html if available, else fall back to regex strip.

    minify-html (pip install minify-html) is a fast Rust-backed minifier that
    works with Python 3.10+ and does not rely on the removed `cgi` module.
    """
    try:
        import minify_html  # type: ignore
        return minify_html.minify(
            source,
            minify_js=False,   # JS is handled separately by jsmin
            minify_css=True,
            remove_processing_instructions=True,
        )
    except ImportError:
        print("  [!] 'minify-html' not installed - using regex fallback.")
        print("       Install with: pip install minify-html")
        # Basic fallback
        minified = re.sub(r'<!--.*?-->', '', source, flags=re.DOTALL)
        minified = re.sub(r'>\s+<', '><', minified)
        minified = re.sub(r'\s{2,}', ' ', minified)
        return minified.strip()


def _try_minify_js_inside_html(source: str) -> str:
    """Minify <script> blocks inside HTML using jsmin if available.

    jsmin (pip install jsmin) works on Python 3.10+.
    """
    try:
        from jsmin import jsmin  # type: ignore

        def _minify_block(m: re.Match) -> str:
            open_tag  = m.group(1)
            js_code   = m.group(2)
            close_tag = m.group(3)
            try:
                minified = jsmin(js_code)
            except Exception:
                minified = js_code
            return open_tag + minified + close_tag

        return re.sub(
            r'(<script[^>]*>)(.*?)(</script>)',
            _minify_block,
            source,
            flags=re.DOTALL | re.IGNORECASE,
        )
    except Exception as exc:
        print("  [!] jsmin unavailable or failed (%s) - JS not minified." % exc)
        print("       Install with: pip install jsmin")
        return source


def _obfuscate_js_inside_html(source: str) -> str:
    """
    Simple variable-name obfuscation for JavaScript inside <script> blocks.

    Strategy
    --------
    •  Collect all user-defined function names and var/let/const identifiers
       that are longer than 3 chars (to avoid mangling short built-ins).
    •  Build a deterministic short-name mapping (_a, _b … _z, _aa …).
    •  Replace every occurrence (whole-word) inside each <script> block.
    •  Identifiers that start with a capital letter (likely constructors /
       Web-API names) and global Web-API names are excluded.
    """
    # Identifiers that must NOT be renamed
    _SAFE = frozenset({
        # Core JS
        'function', 'return', 'var', 'let', 'const', 'new', 'this',
        'true', 'false', 'null', 'undefined', 'typeof', 'instanceof',
        'for', 'while', 'if', 'else', 'switch', 'case', 'break',
        'continue', 'throw', 'try', 'catch', 'finally', 'class',
        'extends', 'super', 'import', 'export', 'default', 'async',
        'await', 'yield', 'delete', 'void', 'in', 'of',
        # Common Web APIs
        'document', 'window', 'console', 'fetch', 'JSON', 'Math',
        'Object', 'Array', 'String', 'Number', 'Boolean', 'Promise',
        'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval',
        'parseInt', 'parseFloat', 'isNaN', 'encodeURIComponent',
        'decodeURIComponent', 'confirm', 'alert',
        # DOM
        'getElementById', 'createElement', 'appendChild', 'removeChild',
        'querySelector', 'querySelectorAll', 'setAttribute', 'getAttribute',
        'classList', 'className', 'innerHTML', 'textContent', 'remove',
        'style', 'value', 'length', 'forEach', 'push', 'then', 'catch',
        'ok', 'json', 'status', 'name', 'ssid', 'rssi', 'encryption',
        'padding', 'margin', 'listStyleType',
        # HTML attribute names that might appear as JS strings
        'href', 'src', 'type', 'onclick', 'method', 'action',
    })

    def _name_gen():
        """Yield _a, _b, …, _z, _aa, _ab, … (infinite)."""
        import string
        letters = string.ascii_lowercase
        length = 1
        while True:
            from itertools import product
            for combo in product(letters, repeat=length):
                yield '_' + ''.join(combo)
            length += 1

    def _obfuscate_block(js_code: str):
        # Find candidate identifiers: word chars, starts with lowercase letter,
        # length > 3, not in safe list
        candidates = set(
            w for w in re.findall(r'\b([a-z][a-zA-Z0-9_]{3,})\b', js_code)
            if w not in _SAFE
        )
        if not candidates:
            return js_code

        gen = _name_gen()
        mapping = {orig: next(gen) for orig in sorted(candidates)}

        result = js_code
        for orig, short in mapping.items():
            result = re.sub(r'\b' + re.escape(orig) + r'\b', short, result)
        return result

    def _process_script(m: re.Match) -> str:
        open_tag  = m.group(1)
        js_code   = m.group(2)
        close_tag = m.group(3)
        return open_tag + _obfuscate_block(js_code) + close_tag

    return re.sub(
        r'(<script[^>]*>)(.*?)(</script>)',
        _process_script,
        source,
        flags=re.DOTALL | re.IGNORECASE,
    )


# ──────────────────────────────────────────────────────────────────────────────
# Utilities
# ──────────────────────────────────────────────────────────────────────────────

def make_c_identifier(name: str) -> str:
    """Convert a filename stem into a valid C identifier."""
    ident = re.sub(r'[^0-9A-Za-z_]', '_', name)
    if re.match(r'^[0-9]', ident):
        ident = '_' + ident
    return ident


def _fmt_bytes(n: int) -> str:
    """Human-readable byte count, e.g. 9.4 KB."""
    if n < 1024:
        return f"{n} B"
    return f"{n / 1024:.2f} KB"


def _reduction(before: int, after: int) -> str:
    """Percentage reduction string."""
    if before == 0:
        return "0.00%"
    pct = (1.0 - after / before) * 100
    return f"{pct:.2f}%"


# ──────────────────────────────────────────────────────────────────────────────
# Main conversion function
# ──────────────────────────────────────────────────────────────────────────────

def html_to_header(filepath: str, do_minify: bool = True, do_obfuscate: bool = True):
    """
    Convert *filepath* (HTML) into a gzip C-array header.

    Parameters
    ----------
    filepath     : Path to the input HTML file.
    do_minify    : If True, minify HTML + inline JS/CSS before compression.
    do_obfuscate : If True, obfuscate JS identifiers inside <script> blocks.
    """
    if not filepath:
        print("Missing argument – filename!")
        sys.exit(1)

    if not os.path.exists(filepath):
        print(f"File not found: {filepath}")
        sys.exit(1)

    base      = os.path.splitext(os.path.basename(filepath))[0]
    safe_base = make_c_identifier(base)
    guard     = f"PAGE_{safe_base.upper()}_H"

    script_dir      = os.path.dirname(os.path.abspath(__file__))
    src_dir         = os.path.normpath(os.path.join(script_dir, "..", "src"))
    os.makedirs(src_dir, exist_ok=True)
    output_filename = os.path.join(src_dir, f"page_{base}.h")

    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # ── Step 1: Read original ────────────────────────────────────────────────
    with open(filepath, "rb") as fh:
        raw_bytes = fh.read()
    original_text = raw_bytes.decode("utf-8", errors="replace")

    size_original = len(raw_bytes)
    print("\n[INPUT] %s" % os.path.abspath(filepath))
    print("   Original size : %s" % _fmt_bytes(size_original))

    current_text = original_text
    size_minified   = size_original   # defaults (unchanged if step skipped)
    size_obfuscated = size_original

    # ── Step 2: Minify ───────────────────────────────────────────────────────
    if do_minify:
        print("\n[2/4] Minifying HTML...")
        current_text = _try_minify_html(current_text)
        current_text = _try_minify_js_inside_html(current_text)
        size_minified = len(current_text.encode("utf-8"))
        print("   Minified size : %s  (reduced by %s)" % (
            _fmt_bytes(size_minified), _reduction(size_original, size_minified)))
    else:
        print("\n[2/4] Minification skipped (--no-minify)")
        size_minified = size_original

    # ── Step 3: Obfuscate ────────────────────────────────────────────────────
    if do_obfuscate:
        print("\n[3/4] Obfuscating JS identifiers...")
        current_text = _obfuscate_js_inside_html(current_text)
        size_obfuscated = len(current_text.encode("utf-8"))
        print("   Obfuscated size : %s  (reduced by %s vs minified)" % (
            _fmt_bytes(size_obfuscated), _reduction(size_minified, size_obfuscated)))
    else:
        print("\n[3/4] Obfuscation skipped (--no-obfuscate)")
        size_obfuscated = size_minified

    # ── Step 4: Gzip ─────────────────────────────────────────────────────────
    print("\n[4/4] GZip compressing (level 9)...")
    processed_bytes = current_text.encode("utf-8")
    compressed      = gzip.compress(processed_bytes, compresslevel=9)
    size_gzip       = len(compressed)
    print("   Compressed size : %s  (reduced by %s vs original)" % (
        _fmt_bytes(size_gzip), _reduction(size_original, size_gzip)))

    # ── Step 5: Write header ─────────────────────────────────────────────────
    print("\n[OUT] Writing header: %s" % os.path.abspath(output_filename))

    with open(output_filename, "w", newline="\n", encoding="utf-8") as out:

        # ── Include guard ────────────────────────────────────────────────────
        out.write(f"#ifndef {guard}\n")
        out.write(f"#define {guard}\n\n")

        # ── Doxygen file doc-block ───────────────────────────────────────────
        out.write("/**\n")
        out.write(f" * @file    page_{base}.h\n")
        out.write(f" * @brief   GZip-compressed C array of the '{base}.html' web page.\n")
        out.write(" *\n")
        out.write(" * @details This file was auto-generated by `html_to_header.py`.\n")
        out.write(" *          The HTML source was processed through the following pipeline:\n")
        out.write(" *\n")
        out.write(" *          <b>Pipeline:</b>\n")
        out.write(" *          @code\n")
        out.write(f" *          Original HTML  ({_fmt_bytes(size_original)})\n")
        if do_minify:
            out.write(f" *            |-> Minification  ({_fmt_bytes(size_minified)}, -{_reduction(size_original, size_minified)})\n")
        else:
            out.write(" *            |-> Minification  [SKIPPED]\n")
        if do_obfuscate:
            out.write(f" *            |-> JS Obfuscation ({_fmt_bytes(size_obfuscated)}, -{_reduction(size_minified, size_obfuscated)})\n")
        else:
            out.write(" *            |-> JS Obfuscation [SKIPPED]\n")
        out.write(f" *            |-> GZip (level 9)  ({_fmt_bytes(size_gzip)}, -{_reduction(size_original, size_gzip)} total)\n")
        out.write(" *          @endcode\n")
        out.write(" *\n")
        out.write(f" * @note    Generated : {timestamp}\n")
        out.write(f" * @note    Serve with Content-Encoding: gzip\n")
        out.write(" *\n")
        out.write(" * @par Size Summary\n")
        out.write(f" *   - Original HTML  : {size_original:>8} B  ({_fmt_bytes(size_original)})\n")
        if do_minify:
            out.write(f" *   - After Minify   : {size_minified:>8} B  ({_fmt_bytes(size_minified)})\n")
        if do_obfuscate:
            out.write(f" *   - After Obfuscate: {size_obfuscated:>8} B  ({_fmt_bytes(size_obfuscated)})\n")
        out.write(f" *   - After GZip     : {size_gzip:>8} B  ({_fmt_bytes(size_gzip)})\n")
        out.write(" */\n\n")

        # ── Doxygen for the array variable ───────────────────────────────────
        out.write("/**\n")
        out.write(f" * @brief  GZip-compressed binary payload of `{base}.html`.\n")
        out.write(" *\n")
        out.write(" *         Send this buffer directly over HTTP with the header:\n")
        out.write(" *         @code\n")
        out.write(" *         Content-Encoding: gzip\n")
        out.write(" *         Content-Type:     text/html; charset=utf-8\n")
        out.write(" *         @endcode\n")
        out.write(" *\n")
        out.write(f" * @see    page_{safe_base}_len  for the exact byte count.\n")
        out.write(" */\n")
        out.write(f"const unsigned char page_{safe_base}[] = {{\n")

        # Write array body (xxd -i style, 16 bytes/line, uppercase hex)
        for i, b in enumerate(compressed):
            if i % 16 == 0:
                out.write("  ")
            out.write(f"0x{b:02X}")
            if i != len(compressed) - 1:
                out.write(", ")
            if (i + 1) % 16 == 0:
                out.write("\n")

        # Handle trailing newline when last row is partial
        if len(compressed) % 16 != 0:
            out.write("\n")

        out.write("};\n\n")

        # ── Doxygen for the length variable ──────────────────────────────────
        out.write("/**\n")
        out.write(f" * @brief  Byte length of the #page_{safe_base} GZip payload.\n")
        out.write(" *\n")
        out.write(f" * @par Size breakdown\n")
        out.write(f" *   - Original HTML  : {size_original} B\n")
        if do_minify:
            out.write(f" *   - After Minify   : {size_minified} B  (-{_reduction(size_original, size_minified)})\n")
        if do_obfuscate:
            out.write(f" *   - After Obfuscate: {size_obfuscated} B  (-{_reduction(size_minified, size_obfuscated)})\n")
        out.write(f" *   - After GZip     : {size_gzip} B  (-{_reduction(size_original, size_gzip)} vs original)\n")
        out.write(" */\n")
        out.write(f"const unsigned int page_{safe_base}_len = {size_gzip};\n\n")

        out.write(f"#endif /* {guard} */\n")

    print("\n[DONE] Generated : %s" % os.path.abspath(output_filename))
    print("   Final payload : %s  (saved %s / %s vs original)\n" % (
        _fmt_bytes(size_gzip),
        _fmt_bytes(size_original - size_gzip),
        _reduction(size_original, size_gzip)))


# ──────────────────────────────────────────────────────────────────────────────
# CLI entry point
# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert an HTML file into a gzip-compressed C header.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("filepath", help="Path to the HTML source file.")
    parser.add_argument(
        "--no-minify",
        action="store_true",
        default=False,
        help="Skip HTML/CSS/JS minification.",
    )
    parser.add_argument(
        "--no-obfuscate",
        action="store_true",
        default=False,
        help="Skip JavaScript identifier obfuscation.",
    )

    args = parser.parse_args()
    html_to_header(
        filepath=args.filepath,
        do_minify=not args.no_minify,
        do_obfuscate=not args.no_obfuscate,
    )