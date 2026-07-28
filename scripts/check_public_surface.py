#!/usr/bin/env python3
"""Detect drift between Janus sources, the public inventory, and its guides."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


TYPE_RE = re.compile(
    r"(?:(private|internal)\s+)?(class|struct|enum|trait)\s+([A-Za-z_]\w*)"
)
FUNCTION_RE = re.compile(
    r"(?:(private|internal)\s+)?(?:(extern)\s+)?(?:(consume)\s+)?"
    r"def\s+([A-Za-z_]\w*)"
)
VALUE_RE = re.compile(r"(?:(private|internal)\s+)?(val|var)\s+([A-Za-z_]\w*)")
FIELD_RE = re.compile(
    r"(?:(private|internal)\s+)?(val|var)\s+([A-Za-z_]\w*)\s*:\s*"
    r"([^,)]+)"
)
VARIANT_RE = re.compile(r"([A-Z][A-Za-z0-9_]*)(?:\s*\(|\s*=|\s*,|$)")
MODULE_RE = re.compile(r"^module\s+([\w.]+)", re.MULTILINE)
OPTION_RE = re.compile(r"(?<!\w)(--?[a-z][a-z0-9-]*)")
INLINE_CODE_RE = re.compile(r"`([^`\n]+)`")
FENCED_CODE_RE = re.compile(r"```.*?```", re.DOTALL)
BUILTIN_TYPES = {
    "BigInt",
    "Ptr",
    "Unit",
    "bool",
    "byte",
    "char",
    "double",
    "float",
    "int",
    "isize",
    "long",
    "short",
    "string",
    "ubyte",
    "uint",
    "ulong",
    "ushort",
    "usize",
}
BUILTIN_FUNCTIONS = {
    "alloc",
    "cstr",
    "debug",
    "extern",
    "free",
    "null",
    "panic",
    "print",
    "println",
    "realloc",
}
NON_API_SUFFIXES = {
    "dll",
    "dylib",
    "janus",
    "lock",
    "md",
    "so",
    "toml",
}


@dataclass
class PendingDeclaration:
    kind: str
    symbol: str
    visibility: str | None
    context: str | None
    lines: list[str]


@dataclass
class ParsedModule:
    name: str
    source: Path
    signatures: dict[str, str]

    @property
    def digest(self) -> str:
        canonical = "".join(
            f"{symbol}\t{self.signatures[symbol]}\n"
            for symbol in sorted(self.signatures)
        )
        return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def normalize_signature(lines: list[str]) -> str:
    signature = " ".join(part.strip() for part in lines if part.strip())
    signature = signature.split("{", 1)[0].strip()
    signature = re.sub(r"\s+", " ", signature)
    signature = re.sub(r"\s*([,:()\[\]])\s*", r"\1", signature)
    signature = re.sub(r"\s*<:\s*", " <: ", signature)
    return signature.rstrip(",")


def declaration_complete(pending: PendingDeclaration) -> bool:
    text = " ".join(pending.lines)
    if "{" in text:
        return True
    if pending.kind == "type":
        return False
    balanced = text.count("(") == text.count(")") and text.count("[") == text.count("]")
    return balanced and ")" in text and ":" in text.rsplit(")", 1)[-1]


def parse_module(path: Path) -> ParsedModule:
    text = path.read_text(encoding="utf-8")
    module_match = MODULE_RE.search(text)
    if module_match is None:
        raise ValueError(f"{path}: déclaration de module absente")

    signatures: dict[str, str] = {}
    depth = 0
    context: str | None = None
    context_kind: str | None = None
    context_depth: int | None = None
    pending: PendingDeclaration | None = None

    def finish(declaration: PendingDeclaration) -> None:
        nonlocal context, context_kind, context_depth
        signature = normalize_signature(declaration.lines)
        public = declaration.visibility is None
        if public:
            signatures[declaration.symbol] = signature
            if declaration.kind == "type":
                for field in FIELD_RE.finditer(signature):
                    visibility, field_kind, name, field_type = field.groups()
                    if visibility is None:
                        signatures[f"{declaration.symbol}.{name}"] = (
                            f"{field_kind} {name}:{field_type.strip()}"
                        )
        if declaration.kind == "type":
            context = declaration.symbol if public else None
            context_kind = (
                TYPE_RE.match(declaration.lines[0]).group(2) if public else None
            )
            context_depth = depth + 1 if public else None

    for raw_line in text.splitlines():
        stripped = raw_line.strip()

        if pending is not None:
            pending.lines.append(stripped)
            if declaration_complete(pending):
                finish(pending)
                pending = None
            depth += raw_line.count("{") - raw_line.count("}")
            if context is not None and context_depth is not None and depth < context_depth:
                context = None
                context_kind = None
                context_depth = None
            continue

        type_match = TYPE_RE.match(stripped) if depth == 0 else None
        if type_match is not None:
            visibility, _, name = type_match.groups()
            pending = PendingDeclaration("type", name, visibility, None, [stripped])
            if declaration_complete(pending):
                finish(pending)
                pending = None
            depth += raw_line.count("{") - raw_line.count("}")
            if context is not None and context_depth is not None and depth < context_depth:
                context = None
                context_kind = None
                context_depth = None
            continue

        at_public_scope = depth == 0 or (
            context is not None and context_depth is not None and depth == context_depth
        )
        function_match = FUNCTION_RE.match(stripped) if at_public_scope else None
        if function_match is not None:
            visibility, _, _, name = function_match.groups()
            symbol = f"{context}.{name}" if context is not None else name
            pending = PendingDeclaration(
                "function", symbol, visibility, context, [stripped]
            )
            if declaration_complete(pending):
                finish(pending)
                pending = None
            depth += raw_line.count("{") - raw_line.count("}")
            continue

        if depth == 0:
            value_match = VALUE_RE.match(stripped)
            if value_match is not None and value_match.group(1) is None:
                _, kind, name = value_match.groups()
                signature = normalize_signature([stripped.split("=", 1)[0]])
                signatures[name] = signature or f"{kind} {name}"

        if (
            context is not None
            and context_kind == "enum"
            and context_depth is not None
            and depth == context_depth
        ):
            variant_match = VARIANT_RE.match(stripped)
            if variant_match is not None:
                name = variant_match.group(1)
                signatures[f"{context}.{name}"] = stripped.rstrip(",")

        depth += raw_line.count("{") - raw_line.count("}")
        if context is not None and context_depth is not None and depth < context_depth:
            context = None
            context_kind = None
            context_depth = None

    if pending is not None:
        raise ValueError(f"{path}: déclaration publique incomplète: {pending.symbol}")
    return ParsedModule(module_match.group(1), path, signatures)


def documentation_label(entry: dict[str, object]) -> str:
    documents = entry.get("documentation", [])
    if not isinstance(documents, list) or not documents:
        return "documentation non déclarée"
    return ", ".join(str(document) for document in documents)


def check_document_citations(
    root: Path, entries: list[dict[str, object]]
) -> list[str]:
    errors: list[str] = []
    module_names = {str(entry["name"]) for entry in entries}
    symbols = {
        str(symbol)
        for entry in entries
        for symbol in entry.get("symbols", [])
    }
    symbol_leaves = {symbol.rsplit(".", 1)[-1] for symbol in symbols}
    documents = {
        str(document)
        for entry in entries
        for document in entry.get("documentation", [])
        if str(document).endswith(".md")
    }

    for document in sorted(documents):
        path = root / document
        if not path.is_file():
            continue
        prose = FENCED_CODE_RE.sub("", path.read_text(encoding="utf-8"))
        for citation in INLINE_CODE_RE.findall(prose):
            token = citation.strip()
            qualified = re.fullmatch(
                r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)(?:\([^`]*\))?", token
            )
            if qualified is not None:
                owner, member = qualified.groups()
                plain = f"{owner}.{member}"
                if member in NON_API_SUFFIXES:
                    continue
                if plain in module_names or plain in symbols:
                    continue
                if owner[:1].islower() and member in symbol_leaves:
                    continue
                errors.append(
                    f"{document}: surface citée `{plain}` absente de l'inventaire"
                )
                continue

            call = re.fullmatch(
                r"([A-Za-z_]\w*)(?:\[[^`]+\])?\([^`]*\)", token
            )
            if call is not None:
                name = call.group(1)
                if name not in symbol_leaves and name not in BUILTIN_FUNCTIONS:
                    errors.append(
                        f"{document}: API citée `{name}` absente de l'inventaire"
                    )
                continue

            generic_type = re.fullmatch(r"([A-Z][A-Za-z0-9_]*)\[[^`]+\]", token)
            if generic_type is not None:
                name = generic_type.group(1)
                if name not in symbol_leaves and name not in BUILTIN_TYPES:
                    errors.append(
                        f"{document}: type cité `{name}` absent de l'inventaire"
                    )
    return errors


def check_inventory(
    root: Path,
    inventory_path: Path,
    janus: Path | None,
    allow_partial: bool,
) -> tuple[list[str], dict[str, str]]:
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    errors: list[str] = []
    if inventory.get("schema_version") != 2:
        errors.append(
            f"{inventory_path}: schema_version doit valoir 2 pour contrôler les signatures"
        )

    actual_modules: dict[str, ParsedModule] = {}
    for source in sorted((root / "stdlib" / "std").rglob("*.janus")):
        parsed = parse_module(source)
        actual_modules[parsed.name] = parsed

    entries = inventory.get("stdlib_modules", [])
    recorded_names = {str(entry["name"]) for entry in entries}
    if len(recorded_names) != len(entries):
        errors.append(f"{inventory_path}: un module est inventorié plusieurs fois")
    if not allow_partial:
        for module in sorted(set(actual_modules) - recorded_names):
            errors.append(
                f"docs/public-surface-0.5.json: module public {module} absent de l'inventaire"
            )
        for module in sorted(recorded_names - set(actual_modules)):
            errors.append(
                f"docs/public-surface-0.5.json: module {module} absent de stdlib/std"
            )

    digests: dict[str, str] = {}
    for entry in entries:
        module = str(entry["name"])
        source = root / str(entry["source"])
        documents = entry.get("documentation", [])
        label = documentation_label(entry)
        if not source.is_file():
            errors.append(f"{label}: source canonique absente pour {module}: {source}")
            continue
        for document in documents:
            document_path = root / str(document)
            if not document_path.is_file():
                errors.append(f"{document}: document déclaré pour {module} introuvable")
        parsed = actual_modules.get(module)
        if parsed is None:
            errors.append(f"{label}: surface {module} absente de la source canonique")
            continue
        if parsed.source.resolve() != source.resolve():
            errors.append(
                f"{label}: source de {module} déclarée comme {entry['source']}, "
                f"mais trouvée dans {parsed.source.relative_to(root)}"
            )

        actual = set(parsed.signatures)
        recorded_list = [str(symbol) for symbol in entry.get("symbols", [])]
        recorded = set(recorded_list)
        if len(recorded) != len(recorded_list):
            errors.append(f"{label}: un symbole de {module} est inventorié plusieurs fois")
        for symbol in sorted(recorded - actual):
            errors.append(
                f"{label}: surface {module}.{symbol} listée dans l'inventaire "
                f"mais absente de {entry['source']}"
            )
        if not allow_partial:
            for symbol in sorted(actual - recorded):
                errors.append(
                    f"{label}: surface {module}.{symbol} présente dans "
                    f"{entry['source']} mais absente de l'inventaire"
                )

        digests[module] = parsed.digest
        expected_digest = entry.get("signature_digest")
        if expected_digest != parsed.digest:
            errors.append(
                f"{label}: signatures de {module} divergentes "
                f"(inventaire {expected_digest!r}, source {parsed.digest!r})"
            )

    errors.extend(check_document_citations(root, entries))

    if janus is not None:
        result = subprocess.run(
            [str(janus), "--help"],
            cwd=root,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            errors.append(f"docs/tooling.md: `janus --help` a échoué: {result.stderr}")
        else:
            help_text = result.stdout + result.stderr
            help_commands: set[str] = set()
            for line in help_text.splitlines():
                match = re.match(r"\s*janus\s+(\S+)", line)
                if match is not None:
                    help_commands.add(match.group(1))
            help_options = set(OPTION_RE.findall(help_text))
            commands = inventory.get("cli", {}).get("commands", [])
            recorded_commands = {str(command["name"]) for command in commands}
            recorded_options = {
                str(option)
                for command in commands
                for option in command.get("options", [])
            }
            recorded_options.update(
                name for name in recorded_commands if name.startswith("-")
            )
            for command in sorted(help_commands - recorded_commands):
                errors.append(
                    f"docs/tooling.md: commande `{command}` de `janus --help` "
                    "absente de l'inventaire"
                )
            for command in sorted(recorded_commands - help_commands):
                errors.append(
                    f"docs/tooling.md: commande inventoriée `{command}` "
                    "absente de `janus --help`"
                )
            for option in sorted(help_options - recorded_options):
                errors.append(
                    f"docs/tooling.md: option `{option}` de `janus --help` "
                    "absente de l'inventaire"
                )
            for option in sorted(recorded_options - help_options):
                errors.append(
                    f"docs/tooling.md: option inventoriée `{option}` "
                    "absente de `janus --help`"
                )

    return errors, digests


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument(
        "--inventory",
        type=Path,
        help="Inventaire à contrôler (par défaut docs/public-surface-0.5.json)",
    )
    parser.add_argument("--janus", type=Path, help="Binaire dont vérifier --help")
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="Autoriser une fixture ne couvrant qu'une partie des modules",
    )
    parser.add_argument(
        "--print-digests",
        action="store_true",
        help="Afficher les empreintes de signatures extraites",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    inventory = (
        args.inventory.resolve()
        if args.inventory is not None
        else root / "docs" / "public-surface-0.5.json"
    )
    errors, digests = check_inventory(root, inventory, args.janus, args.allow_partial)
    if args.print_digests:
        print(json.dumps(digests, indent=2, sort_keys=True))
    if errors:
        for error in errors:
            print(f"ERROR {error}", file=sys.stderr)
        return 1
    print(
        f"Surface publique vérifiée: {len(digests)} modules, "
        f"inventaire {inventory.relative_to(root)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
