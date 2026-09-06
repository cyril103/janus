#!/usr/bin/env python3
"""Inventory public generic constructors and redundant literal casts for #275."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOTS = (
    Path("README.md"),
    Path("docs"),
    Path("website/docs"),
    Path("examples"),
    Path("stdlib"),
)
SUFFIXES = {".md", ".janus", ".html", ".json"}
REPORT = Path("docs/audits/constructor-inference-0.22.md")
GENERATED_MIRRORS = (
    "website/docs/reference/generated/",
    "website/docs/reference/stdlib/api-index.json",
    "website/docs/reference/stdlib/index.html",
)
NEW_TOKEN = re.compile(r"\bnew\b")
TYPE_NAME = re.compile(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*")
USIZE_LITERAL = re.compile(r"\busize\(\s*[0-9][0-9_]*\s*\)")
# Exact source occurrences intentionally kept explicit. New occurrences are
# classified as simplifiable and make --check fail until reviewed.
CONSTRUCTOR_EXCEPTIONS = {
    ("README.md", "new Factory[int](", 112, 2): "pédagogique explicite",
    ("docs/archive/migration-0.5-to-0.6.md", "new Array[Resource](", 31, 35): "historique",
    ("docs/language-guide.md", "new Factory[int](", 1296, 16): "pédagogique explicite",
    ("website/docs/book/07-generiques-closures.md", "new Pair[string, int](", 37, 5): "pédagogique explicite",
    ("examples/generic_classes.janus", "new Box[int](", 17, 31): "pédagogique explicite",
    ("examples/generic_classes.janus", "new Box[string](", 22, 30): "pédagogique explicite",
    ("stdlib/std/process.janus", "new Array[string](", 8, 16): "pédagogique (inférence impossible)",

}

# Keys are path, spelling and one-based line. Line anchoring is deliberate: a
# newly added cast in the same document must be reviewed instead of inheriting
# a broad path-level exemption.
CAST_EXCEPTIONS: dict[tuple[str, str, int, int], str] = {
    ("docs/archive/migration-0.5-to-0.6.md", "usize(2)", 31, 55): "historique",
    ("docs/archive/migration-0.5-to-0.6.md", "usize(0)", 39, 23): "historique",
    ("docs/audits/stdlib-0.7.4.md", "usize(1)", 156, 20): "donnée d’audit historique",
    ("docs/audits/stdlib-0.7.4.md", "usize(0)", 158, 24): "donnée d’audit historique",
    ("docs/design/container-ownership.md", "usize(0)", 90, 21): "pédagogique (index usize)",
    ("docs/design/container-ownership.md", "usize(0)", 175, 21): "pédagogique (index usize)",
    ("docs/design/container-ownership.md", "usize(0)", 176, 45): "pédagogique (index usize)",
    ("docs/design/container-ownership.md", "usize(0)", 189, 19): "pédagogique (index usize)",
    ("docs/design/lexical-borrowing.md", "usize(0)", 491, 41): "pédagogique (index usize)",
    ("examples/casts.janus", "usize(1)", 11, 38): "pédagogique (casts)",
    ("examples/casts.janus", "usize(0)", 12, 16): "pédagogique (casts)",
    ("examples/casts.janus", "usize(0)", 13, 34): "pédagogique (casts)",
    ("examples/array.janus", "usize(10)", 29, 41): "nécessaire (opérande binaire usize)",
    ("examples/pointers.janus", "usize(2)", 2, 38): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(0)", 3, 16): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(1)", 4, 16): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(0)", 6, 33): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(1)", 7, 34): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(4)", 10, 34): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(2)", 17, 16): "pédagogique (API pointeur)",
    ("examples/pointers.janus", "usize(2)", 24, 34): "pédagogique (API pointeur)",
    ("examples/usize.janus", "usize(0)", 4, 25): "pédagogique (usize)",
    ("examples/usize.janus", "usize(0)", 5, 25): "pédagogique (usize)",
    ("examples/usize.janus", "usize(1)", 9, 25): "pédagogique (usize)",
    ("examples/snake/main.janus", "usize(1)", 33, 25): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(0)", 107, 19): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 108, 25): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(0)", 110, 39): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(0)", 111, 41): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 308, 36): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 315, 45): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 323, 68): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 327, 56): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(0)", 328, 35): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 329, 60): "nécessaire (opérande binaire usize)",
    ("examples/snake/main.janus", "usize(1)", 330, 41): "nécessaire (opérande binaire usize)",
    ("docs/language-guide.md", "usize(1)", 1655, 43): "nécessaire (opérande binaire usize)",
    ("docs/stdlib-reference.md", "usize(2)", 37, 41): "nécessaire (opérande binaire usize)",
    ("docs/stdlib-reference.md", "usize(8)", 355, 44): "nécessaire (opérande binaire usize)",
    ("docs/stdlib-reference.md", "usize(5)", 500, 55): "nécessaire (opérande binaire usize)",
    ("docs/stdlib-reference.md", "usize(2)", 527, 49): "nécessaire (opérande binaire usize)",
    ("docs/stdlib-reference.md", "usize(6)", 655, 45): "nécessaire (opérande binaire usize)",
    ("docs/stdlib-reference.md", "usize(6)", 667, 32): "nécessaire (opérande binaire usize)",
    ("stdlib/std/random.janus", "usize(1)", 8, 44): "nécessaire (opérande binaire usize)",
    ("website/docs/book/06-collections-iterateurs.md", "usize(5)", 127, 28): "nécessaire (opérande binaire usize)",
    ("website/docs/book/06-collections-iterateurs.md", "usize(1)", 134, 32): "nécessaire (opérande binaire usize)",
    ("website/docs/book/06-collections-iterateurs.md", "usize(3)", 134, 60): "nécessaire (opérande binaire usize)",

    ("website/docs/book/02-valeurs-types.md", "usize(1)", 122, 33): "pédagogique (arithmétique usize)",
    ("website/docs/book/09-propriete-avancee.md", "usize(16)", 54, 39): "pédagogique (API pointeur)",
    ("website/docs/book/10-modules-visibilite-ffi.md", "usize(4)", 121, 34): "pédagogique (FFI)",
    ("website/docs/book/10-modules-visibilite-ffi.md", "usize(0)", 123, 12): "pédagogique (FFI)",
    ("website/docs/book/10-modules-visibilite-ffi.md", "usize(0)", 124, 30): "pédagogique (FFI)",
    ("website/docs/tutorials/collections.md", "usize(1)", 80, 18): "pédagogique (signature take)",
    ("website/docs/tutorials/propriete-move-consume.md", "usize(16)", 94, 39): "pédagogique (API pointeur)",
}


def files(root: Path):
    for entry in ROOTS:
        path = root / entry
        if path.is_file():
            yield path
        elif path.exists():
            yield from sorted(
                item
                for item in path.rglob("*")
                if item.is_file()
                and item.suffix in SUFFIXES
                and item.relative_to(root) != REPORT
            )


def skip_trivia(text: str, cursor: int) -> int:
    """Skip whitespace and comments, rejecting unterminated block comments."""
    while True:
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        if text.startswith("//", cursor):
            newline = text.find("\n", cursor + 2)
            cursor = len(text) if newline < 0 else newline + 1
            continue
        if text.startswith("/*", cursor):
            close = text.find("*/", cursor + 2)
            if close < 0:
                raise ValueError("unterminated comment in generic constructor")
            cursor = close + 2
            continue
        return cursor


def constructors(
    text: str, *, include_doc_comments: bool = False, include_strings: bool = False
):
    """Yield balanced generic constructor spellings, including multiline ones."""
    search_from = 0
    while search_from < len(text):
        match = NEW_TOKEN.search(text, search_from)
        if match is None:
            return

        # Ignore ordinary comments before `new`. Public `///` documentation
        # comments are scanned only for generated API mirrors and stdlib docs.
        cursor = search_from
        hidden = False
        resume_at = match.end()
        while cursor < match.start():
            if not include_strings and text[cursor] == '"':
                quote = text[cursor]
                end = cursor + 1
                while end < len(text):
                    if text[end] == "\\":
                        end += 2
                        continue
                    if text[end] == quote:
                        end += 1
                        break
                    end += 1
                if match.start() < end:
                    hidden = True
                    resume_at = end
                    break
                cursor = end
                continue
            if text.startswith("//", cursor):
                if include_doc_comments and text.startswith("///", cursor):
                    cursor += 3
                    continue
                newline = text.find("\n", cursor + 2)
                if newline < 0 or match.start() < newline:
                    hidden = True
                    resume_at = len(text) if newline < 0 else newline + 1
                    break
                cursor = newline + 1
                continue
            if text.startswith("/*", cursor):
                close = text.find("*/", cursor + 2)
                if close < 0:
                    return
                if match.start() < close + 2:
                    hidden = True
                    resume_at = close + 2
                    break
                cursor = close + 2
                continue
            cursor += 1
        if hidden:
            search_from = resume_at
            continue

        cursor = skip_trivia(text, match.end())
        type_name = TYPE_NAME.match(text, cursor)
        if type_name is None:
            search_from = match.end()
            continue
        cursor = skip_trivia(text, type_name.end())
        if cursor >= len(text) or text[cursor] != "[":
            search_from = type_name.end()
            continue
        depth = 1
        cursor += 1
        while cursor < len(text) and depth:
            if text.startswith("//", cursor) or text.startswith("/*", cursor):
                cursor = skip_trivia(text, cursor)
                continue
            if text[cursor] == "[":
                depth += 1
            elif text[cursor] == "]":
                depth -= 1
            cursor += 1
        if depth:
            raise ValueError("unbalanced generic constructor type arguments")
        after = skip_trivia(text, cursor)
        if after < len(text) and text[after] == "]":
            raise ValueError("unexpected closing bracket after generic constructor")
        if after >= len(text) or text[after] != "(":
            search_from = after
            continue
        spelling = re.sub(r"\s+", " ", text[match.start() : after + 1])
        yield match.start(), spelling
        search_from = after + 1


def lexically_visible(
    text: str,
    position: int,
    *,
    include_doc_comments: bool = False,
    include_strings: bool = False,
) -> bool:
    """Return whether a position is outside ignored comments and strings."""
    cursor = 0
    while cursor < position:
        if not include_strings and text[cursor] == '"':
            cursor += 1
            while cursor < len(text):
                if text[cursor] == "\\":
                    cursor += 2
                    continue
                if text[cursor] == '"':
                    cursor += 1
                    break
                cursor += 1
            if position < cursor:
                return False
            continue
        if text.startswith("//", cursor):
            if include_doc_comments and text.startswith("///", cursor):
                cursor += 3
                continue
            newline = text.find("\n", cursor + 2)
            end = len(text) if newline < 0 else newline + 1
            if position < end:
                return False
            cursor = end
            continue
        if text.startswith("/*", cursor):
            close = text.find("*/", cursor + 2)
            if close < 0:
                return False
            if position < close + 2:
                return False
            cursor = close + 2
            continue
        cursor += 1
    return True


def classification(
    relative: str, spelling: str, line: int, column: int, kind: str
) -> tuple[str, str]:
    if kind == "constructeur":
        rationale = CONSTRUCTOR_EXCEPTIONS.get(
            (relative, spelling, line, column)
        )
    else:
        rationale = CAST_EXCEPTIONS.get((relative, spelling, line, column))
    if rationale is None:
        return "simplifiable", "à migrer"
    if rationale.startswith("pédagogique") or rationale == "historique":
        return "pédagogique explicite", rationale
    return "test de couverture", rationale


def render(root: Path) -> str:
    rows: list[tuple[str, int, int, str, str, str, str]] = []
    for path in files(root):
        relative = path.relative_to(root).as_posix()
        if any(relative.startswith(prefix) for prefix in GENERATED_MIRRORS):
            continue
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        include_doc_comments = relative.startswith("stdlib/")
        for start, spelling in constructors(
            text,
            include_doc_comments=include_doc_comments,
            include_strings=relative.endswith("api-index.json"),
        ):
            number = text.count("\n", 0, start) + 1
            column = start - text.rfind("\n", 0, start)
            if relative.startswith("stdlib/") and not lines[number - 1].lstrip().startswith("///"):
                continue
            category, rationale = classification(
                relative, spelling, number, column, "constructeur"
            )
            rows.append(
                (relative, number, column, "constructeur", spelling,
                 category, rationale)
            )
        for match in USIZE_LITERAL.finditer(text):
            number = text.count("\n", 0, match.start()) + 1
            column = match.start() - text.rfind("\n", 0, match.start())
            if not lexically_visible(
                text,
                match.start(),
                include_doc_comments=include_doc_comments,
                include_strings=relative.endswith("api-index.json"),
            ):
                continue
            if relative.startswith("stdlib/") and not lines[number - 1].lstrip().startswith("///"):
                continue
            spelling = re.sub(r"\s+", "", match.group(0))
            category, rationale = classification(
                relative, spelling, number, column, "cast littéral"
            )
            rows.append(
                (relative, number, column, "cast littéral", spelling,
                 category, rationale)
            )

    rows.sort(key=lambda row: (row[0], row[1], row[2], row[3], row[4]))
    lines = [
        "# Audit de l’inférence des constructeurs génériques",
        "",
        "Rapport généré. Reproduction :",
        "",
        "```bash",
        "python3 scripts/audit_constructor_inference.py --check",
        "```",
        "",
        "L’inventaire couvre les constructeurs génériques explicites, y compris les",
        "formes multilignes et imbriquées, ainsi que tous les casts `usize(<littéral>)`",
        "des surfaces publiques. Les occurrences simplifiables ont été migrées ; les",
        "formes restantes sont pédagogiques ou constituent une couverture explicite.",
        "Les miroirs générés ne sont pas dupliqués dans la table : leur identité avec",
        "les sources canoniques est vérifiée séparément par les tests du site et de l’API.",
        "",
        "| Fichier | Ligne:colonne | Nature | Occurrence | Classification | Justification |",
        "|---|---:|---|---|---|---|",
    ]
    for relative, number, column, kind, spelling, category, rationale in rows:
        escaped = spelling.replace("|", "\\|")
        lines.append(
            f"| `{relative}` | {number}:{column} | {kind} | `{escaped}` | "
            f"{category} | {rationale} |"
        )
    simplifiable = sum(row[-2] == "simplifiable" for row in rows)
    lines.extend(
        (
            "",
            f"Total : **{len(rows)}** occurrences restantes, dont "
            f"**{simplifiable}** simplifiable.",
            "",
        )
    )
    return "\n".join(lines)


def self_test() -> None:
    sample = (
        "new Box /* before */ [Pair[int, /* ] ignored */ Array[string]]] "
        "/* after */ (42)"
    )
    found = list(constructors(sample))
    if len(found) != 1 or "/* ] ignored */" not in found[0][1]:
        raise AssertionError("commented, nested constructor was not inventoried")
    try:
        list(constructors("new Box[Pair[int](42)"))
    except ValueError:
        pass
    else:
        raise AssertionError("unbalanced constructor was not rejected")
    try:
        list(constructors("new Box[int]](1)"))
    except ValueError:
        pass
    else:
        raise AssertionError("extra generic closing bracket was not rejected")
    for malformed in (
        "new Box /* unterminated [",
        "new Box[int /* unterminated ](1)",
        "new Box[int] /* unterminated (1)",
    ):
        try:
            list(constructors(malformed))
        except ValueError:
            pass
        else:
            raise AssertionError("malformed constructor comment was not rejected")
    if list(constructors("// new Ghost[int(1)\n/* new Phantom[int](1) */")):
        raise AssertionError("constructor inside ordinary comment was inventoried")
    if list(constructors('"new Quoted[int](1)"')):
        raise AssertionError("constructor inside string was inventoried")
    if len(list(constructors('"new Mirrored[int](1)"', include_strings=True))) != 1:
        raise AssertionError("constructor inside generated JSON string was omitted")
    prose = "L’inférence d’un type permet new Factory[int](1)"
    if len(list(constructors(prose))) != 1:
        raise AssertionError("French apostrophe hid public constructor")
    cast_probe = '// usize(1)\n"usize(2)"\nusize(3)'
    visible_casts = [
        match.group(0)
        for match in USIZE_LITERAL.finditer(cast_probe)
        if lexically_visible(cast_probe, match.start())
    ]
    if visible_casts != ["usize(3)"]:
        raise AssertionError("casts inside comments or strings were inventoried")
    documented = list(
        constructors("/// new Documented[int](1)", include_doc_comments=True)
    )
    if len(documented) != 1:
        raise AssertionError("constructor inside public doc comment was omitted")
    between = list(constructors("new /* comment */ Box[int](1)"))
    if len(between) != 1:
        raise AssertionError("comment between new and type hid constructor")
    unknown = classification(
        "docs/new.md", "new Box[int](", 1, 1, "constructeur"
    )
    if unknown[0] != "simplifiable":
        raise AssertionError("unknown constructor inherited an exemption")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    root = Path(__file__).resolve().parents[1]
    expected = render(root)
    report = root / REPORT
    if args.write:
        report.write_text(expected, encoding="utf-8")
    stale = not report.exists() or report.read_text(encoding="utf-8") != expected
    if args.check and stale:
        print(f"{REPORT} is stale; run: python3 {Path(__file__).relative_to(root)} --write")
        return 1
    if "| simplifiable |" in expected:
        print("public examples still contain simplifiable constructor or cast spellings")
        return 1
    if not args.check and not args.write:
        print(expected, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
