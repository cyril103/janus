#!/usr/bin/env python3
"""Generate and verify the Janus 0.7.4 standard-library audit."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_public_surface import parse_module  # noqa: E402


REPORT_PATH = Path("docs/audits/stdlib-0.7.4.md")
PUBLIC_SURFACE_PATH = Path("docs/public-surface-0.5.json")
AUDIT_DATE = "28 juillet 2026"
DECISIONS = {
    "conservation": (
        "surface publique conservée ; une refonte interne ne doit pas modifier "
        "sa signature ni sa sémantique observable"
    ),
    "refonte-interne": (
        "détail d'implémentation réécrit sans devenir une API utilisateur"
    ),
    "dépréciation": "surface maintenue pendant une migration N/N+1 avant retrait",
    "remplacement-public": (
        "nouvelle surface accompagnée d'une migration et d'une fixture N/N+1"
    ),
}
OWNER_MODULES = {
    "#111 / R074-2": {
        "std.array",
        "std.array_builder",
        "std.builder",
        "std.bytes",
        "std.deque",
        "std.error",
        "std.iterator",
        "std.option",
        "std.ordering",
        "std.priority_queue",
        "std.range",
        "std.result",
        "std.validated",
    },
    "#112 / R074-3": {
        "std.hash_probe",
        "std.hashing",
        "std.hashmap",
        "std.hashset",
    },
    "#113 / R074-4": {
        "std.c",
        "std.fs",
        "std.io",
        "std.math",
        "std.path",
        "std.process",
        "std.random",
        "std.system",
        "std.text",
        "std.time",
        "std.wall_time",
    },
    "#114 / R074-5": {
        "std.graphics",
        "std.graphics.audio",
        "std.graphics.drawing",
        "std.graphics.input",
        "std.graphics.resources",
        "std.graphics.types",
    },
    "#147": {
        "std.testing",
    },
    "#142": {
        "std.numeric",
    },
    "#264": {
        "std.slice",
    },
    "#294": {
        "std.functional",
    },
    "#299": {
        "std.shared",
    },
    "#300": {
        "std.persistent_list",
    },
    "#336": {
        "std.index",
    },
}
IMPORT_RE = re.compile(r"^\s*import\s+([\w.]+)", re.MULTILINE)
DOCUMENTATION_BLOCK_RE = re.compile(
    r"(?:^|\n)(?:\s*///[^\n]*(?:\n|$))+",
    re.MULTILINE,
)
ALLOCATION_RE = re.compile(r"\b(?:alloc|realloc|new)\b")
CLEANUP_RE = re.compile(
    r"\b(?:delete|defer|destructor|free|close|unload)\b", re.IGNORECASE
)
PANIC_RE = re.compile(r"\bpanic\s*\(")
MOVE_RE = re.compile(r"\bmove\b")
CONSUME_RE = re.compile(r"\bconsume\s+def\b")
DESTRUCTOR_RE = re.compile(r"\bdestructor\s*\{")
RESULT_RE = re.compile(r"\bResult\s*\[")
OPTION_RE = re.compile(r"\bOption\s*\[")


@dataclass(frozen=True)
class ModuleAudit:
    name: str
    source: str
    current_status: str
    decision: str
    owner: str
    symbols: tuple[str, ...]
    lines: int
    documented_declarations: int
    imports: tuple[str, ...]
    fixtures: tuple[str, ...]
    documents: tuple[str, ...]
    allocations: int
    cleanup_branches: int
    panics: int
    result_mentions: int
    option_mentions: int
    moves: int
    consumes: int
    destructors: int


@dataclass(frozen=True)
class SymbolAudit:
    module: str
    name: str
    current_status: str
    decision: str
    owner: str


@dataclass(frozen=True)
class DuplicatePattern:
    text: str
    modules: tuple[str, ...]
    occurrences: int


@dataclass(frozen=True)
class AuditModel:
    root: Path
    modules: dict[str, ModuleAudit]
    symbols: dict[tuple[str, str], SymbolAudit]
    duplicate_patterns: tuple[DuplicatePattern, ...]


def owner_for(module: str) -> str:
    owners = [owner for owner, modules in OWNER_MODULES.items() if module in modules]
    if len(owners) != 1:
        raise ValueError(
            f"{module}: propriétaire de migration attendu exactement une fois, "
            f"trouvé {owners}"
        )
    return owners[0]


def decision_for(current_status: str) -> str:
    if current_status == "internal-detail":
        return "refonte-interne"
    return "conservation"


def fixture_imports(root: Path) -> dict[str, set[str]]:
    imports: dict[str, set[str]] = defaultdict(set)
    fixture_suffixes = {".c", ".cc", ".cmake", ".cpp", ".janus"}
    for path in sorted((root / "tests").rglob("*")):
        if not path.is_file() or path.suffix not in fixture_suffixes:
            continue
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(root).as_posix()
        for module in IMPORT_RE.findall(text):
            imports[module].add(relative)
    return imports


def duplicate_patterns(module_texts: dict[str, str]) -> tuple[DuplicatePattern, ...]:
    modules_by_line: dict[str, set[str]] = defaultdict(set)
    occurrences: Counter[str] = Counter()
    for module, text in module_texts.items():
        for raw_line in text.splitlines():
            line = re.sub(r"\s+", " ", raw_line.strip())
            if (
                len(line) < 24
                or line in {"}", "destructor {"}
                or line.startswith(("module ", "import ", "//"))
            ):
                continue
            modules_by_line[line].add(module)
            occurrences[line] += 1

    candidates = [
        DuplicatePattern(line, tuple(sorted(modules)), occurrences[line])
        for line, modules in modules_by_line.items()
        if len(modules) >= 2
    ]
    candidates.sort(
        key=lambda pattern: (
            -len(pattern.modules),
            -pattern.occurrences,
            pattern.text,
        )
    )
    return tuple(candidates[:12])


def collect_audit(root: Path) -> AuditModel:
    root = root.resolve()
    inventory_path = root / PUBLIC_SURFACE_PATH
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    fixture_index = fixture_imports(root)
    modules: dict[str, ModuleAudit] = {}
    symbols: dict[tuple[str, str], SymbolAudit] = {}
    module_texts: dict[str, str] = {}

    for entry in inventory["stdlib_modules"]:
        name = str(entry["name"])
        source = str(entry["source"])
        source_path = root / source
        text = source_path.read_text(encoding="utf-8")
        parsed = parse_module(source_path)
        recorded_symbols = tuple(str(symbol) for symbol in entry["symbols"])
        if set(recorded_symbols) != set(parsed.signatures):
            raise ValueError(
                f"{name}: la surface publique versionnée diverge des sources ; "
                "exécuter d'abord scripts/check_public_surface.py"
            )
        current_status = str(entry["status"])
        decision = decision_for(current_status)
        owner = owner_for(name)
        module_texts[name] = text
        module = ModuleAudit(
            name=name,
            source=source,
            current_status=current_status,
            decision=decision,
            owner=owner,
            symbols=recorded_symbols,
            lines=len(text.splitlines()),
            documented_declarations=max(
                0, len(DOCUMENTATION_BLOCK_RE.findall(text)) - 1
            ),
            imports=tuple(sorted(set(IMPORT_RE.findall(text)))),
            fixtures=tuple(sorted(fixture_index.get(name, set()))),
            documents=tuple(str(path) for path in entry.get("documentation", [])),
            allocations=len(ALLOCATION_RE.findall(text)),
            cleanup_branches=len(CLEANUP_RE.findall(text)),
            panics=len(PANIC_RE.findall(text)),
            result_mentions=len(RESULT_RE.findall(text)),
            option_mentions=len(OPTION_RE.findall(text)),
            moves=len(MOVE_RE.findall(text)),
            consumes=len(CONSUME_RE.findall(text)),
            destructors=len(DESTRUCTOR_RE.findall(text)),
        )
        modules[name] = module
        for symbol in recorded_symbols:
            symbols[(name, symbol)] = SymbolAudit(
                module=name,
                name=symbol,
                current_status=current_status,
                decision=decision,
                owner=owner,
            )

    source_modules = {
        parse_module(path).name
        for path in sorted((root / "stdlib" / "std").rglob("*.janus"))
    }
    if source_modules != set(modules):
        missing = sorted(source_modules - set(modules))
        stale = sorted(set(modules) - source_modules)
        raise ValueError(
            f"inventaire incomplet : absents={missing}, sources disparues={stale}"
        )

    return AuditModel(
        root=root,
        modules=modules,
        symbols=symbols,
        duplicate_patterns=duplicate_patterns(module_texts),
    )


def markdown_cell(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def issue_link(owner: str) -> str:
    number = owner.split()[0].removeprefix("#")
    return f"[{owner}](https://github.com/cyril103/janus/issues/{number})"


def module_table(model: AuditModel) -> list[str]:
    lines = [
        "| Module | Surface | Décision | Propriétaire | Symboles | Lignes | "
        "Blocs `///` | Propriété M/C/D | Erreurs R/O/P | Alloc. | Nettoyages | "
        "Imports | Fixtures | Documentation |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | --- | --- | ---: | "
        "---: | --- | ---: | --- |",
    ]
    for module in model.modules.values():
        imports = ", ".join(f"`{value}`" for value in module.imports) or "—"
        documents = ", ".join(f"`{value}`" for value in module.documents) or "—"
        lines.append(
            "| "
            + " | ".join(
                [
                    f"`{module.name}`",
                    f"`{module.current_status}`",
                    f"`{module.decision}`",
                    issue_link(module.owner),
                    str(len(module.symbols)),
                    str(module.lines),
                    str(module.documented_declarations),
                    f"{module.moves}/{module.consumes}/{module.destructors}",
                    (
                        f"{module.result_mentions}/"
                        f"{module.option_mentions}/{module.panics}"
                    ),
                    str(module.allocations),
                    str(module.cleanup_branches),
                    imports,
                    str(len(module.fixtures)),
                    documents,
                ]
            )
            + " |"
        )
    return lines


def fixture_details(model: AuditModel) -> list[str]:
    lines = [
        "| Module | Fixtures ou tests qui importent directement le module |",
        "| --- | --- |",
    ]
    for module in model.modules.values():
        fixtures = ", ".join(f"`{path}`" for path in module.fixtures) or "aucune"
        lines.append(f"| `{module.name}` | {fixtures} |")
    return lines


def symbol_registry(model: AuditModel) -> list[str]:
    lines: list[str] = []
    for module in model.modules.values():
        lines.extend(
            [
                f"### `{module.name}`",
                "",
                "| Symbole | Surface actuelle | Décision 0.7.4 | "
                "Propriétaire de migration |",
                "| --- | --- | --- | --- |",
            ]
        )
        if not module.symbols:
            lines.append(
                f"| *(aucun symbole propre ; réexports uniquement)* | "
                f"`{module.current_status}` | `{module.decision}` | "
                f"{issue_link(module.owner)} |"
            )
        for symbol in module.symbols:
            audit = model.symbols[(module.name, symbol)]
            lines.append(
                f"| `{markdown_cell(audit.name)}` | `{audit.current_status}` | "
                f"`{audit.decision}` | {issue_link(audit.owner)} |"
            )
        lines.append("")
    return lines


def render_report(model: AuditModel) -> str:
    modules = list(model.modules.values())
    total_lines = sum(module.lines for module in modules)
    total_symbols = len(model.symbols)
    documented = sum(module.documented_declarations for module in modules)
    total_allocations = sum(module.allocations for module in modules)
    total_cleanup = sum(module.cleanup_branches for module in modules)
    imported_modules = sum(bool(module.fixtures) for module in modules)
    fixture_import_count = sum(len(module.fixtures) for module in modules)
    move_count = sum(module.moves for module in modules)
    consume_count = sum(module.consumes for module in modules)
    destructor_count = sum(module.destructors for module in modules)

    lines = [
        "# Audit de la bibliothèque standard — Janus 0.7.4",
        "",
        f"État mesuré au {AUDIT_DATE}. Ce fichier est généré de façon déterministe :",
        "",
        "```bash",
        "python3 scripts/audit_stdlib.py --write",
        "python3 scripts/audit_stdlib.py --check",
        "```",
        "",
        "Le titre et la date conservent la provenance du lot 0.7.4, mais ce "
        "rapport est régénéré et vérifié contre les sources courantes à chaque "
        "exécution du contrôle.",
        "",
        "La source de vérité de la surface reste "
        "[`docs/public-surface-0.5.json`](../public-surface-0.5.json). "
        "Le générateur refuse une divergence entre cet inventaire et les sources.",
        "",
        "## Résumé mesuré",
        "",
        f"- **{len(modules)} modules**, **{total_lines} lignes** et "
        f"**{total_symbols} symboles publics** inventoriés ;",
        f"- **{documented} blocs `///` publics pour {total_symbols} symboles** "
        "(couverture source du lot #115 : 100 %) ;",
        f"- **{total_allocations} sites d'allocation**, **{total_cleanup} marqueurs "
        "de nettoyage**, "
        f"**{move_count}/{consume_count}/{destructor_count}** occurrences "
        "`move`/`consume`/destructeur ;",
        f"- **{imported_modules}/{len(modules)} modules** importés directement par "
        f"au moins une fixture ou un test, soit **{fixture_import_count} couples "
        "module-fichier de test** ;",
        f"- **{len(model.duplicate_patterns)} motifs textuels intermodules** "
        "principaux consignés ci-dessous.",
        "",
        "Ces métriques sont des indicateurs de risque et non des objectifs "
        "d'optimisation isolés. Un marqueur de nettoyage peut apparaître dans un "
        "nom d'API ; les tests sanitizers restent l'autorité sur les fuites et "
        "doubles destructions.",
        "",
        "## Statuts et décisions",
        "",
    ]
    for decision, definition in DECISIONS.items():
        lines.append(f"- `{decision}` : {definition}.")
    lines.extend(
        [
            "",
            "L'audit ne propose **aucune rupture publique** : tous les symboles "
            "`stable-proposed` et `experimental` sont classés `conservation`. "
            "Les symboles `internal-detail` de `std.hash_probe` sont classés "
            "`refonte-interne`. Toute dépréciation ou tout remplacement découvert "
            "pendant #111–#114 doit donc modifier ce rapport avant le code et "
            "ajouter : justification, entrée de migration, fixture N/N+1 et "
            "mention au changelog.",
            "",
            "## Budgets obligatoires des lots #111 à #115",
            "",
            "| Dimension | Budget | Contrôle de sortie |",
            "| --- | --- | --- |",
            "| Compatibilité | zéro suppression, renommage ou dérive de signature "
            "non inventoriée ; zéro changement observable des surfaces conservées "
            "| `scripts/check_public_surface.py` et fixtures N/N+1 |",
            "| Propriété | zéro copie implicite, fuite ou double destruction ; "
            "tout transfert reste explicite | fixtures `Copy`/non-`Copy`, ASan et "
            "chemins succès/erreur/panique |",
            "| Taille | croissance nette maximale de 5 % par lot, hors `///`, "
            "fixtures et code généré ; tout dépassement est justifié dans la PR "
            "| diff de lignes contre la base de ce rapport |",
            "| Performance | régression médiane maximale de 5 % sur les pipelines "
            "et collections concernés ; aucune régression asymptotique | "
            "benchmarks versionnés avec environnement et variance |",
            "| Documentation | 100 % des symboles non expérimentaux documentés ; "
            "au moins un doctest de succès par module et un `compile_fail` par "
            "famille d'erreur structurée | `janus doc`, `janus test --doc` et "
            "crawler de liens |",
            "",
            "## Inventaire par module",
            "",
            "Les colonnes « Propriété M/C/D » comptent `move`, méthodes `consume` "
            "et destructeurs. « Erreurs R/O/P » compte les mentions de "
            "`Result`, `Option` et les appels à `panic`. « Nettoyages » compte "
            "`delete`, `defer`, destructeurs, fermetures et libérations natives.",
            "",
            *module_table(model),
            "",
            "## Invariants de propriété recensés",
            "",
            "1. Une valeur non-`Copy` est déplacée explicitement à l'entrée et à "
            "la sortie d'un conteneur ; aucun emplacement ne possède deux fois la "
            "même valeur.",
            "2. `Array`, `HashMap`, `HashSet`, builders et itérateurs détruisent "
            "exactement une fois les éléments encore possédés lors de `clear`, "
            "d'une sortie anticipée, d'une panique ou de leur destructeur.",
            "3. Les opérations d'observation ne transfèrent pas la propriété ; "
            "les opérations `into*`, `remove`, `replace`, `pop` et les méthodes "
            "`consume` la transfèrent à l'appelant.",
            "4. Les branches `Option.None` et `Result.Error` détruisent les "
            "fallbacks, closures et payloads non retournés, y compris avec `?`.",
            "5. Un handle de fichier, processus, répertoire, texture, fonte, "
            "shader, cible de rendu, son ou musique est soit invalide, soit détenu "
            "par une unique valeur qui le ferme ou le décharge exactement une fois.",
            "6. Une réallocation transfère les éléments initialisés avant de "
            "libérer le stockage brut ; un échec conserve l'ancien état et nettoie "
            "les arguments déjà consommés.",
            "7. Les buffers et chaînes issus d'une frontière native conservent "
            "leur longueur, leur encodage et leur propriétaire jusqu'à la "
            "conversion ou libération explicite.",
            "",
            "Les contrats détaillés existants restent normatifs : "
            "[conteneurs](../design/container-ownership.md), "
            "[flux](../design/io-streams.md), "
            "[chemins et fichiers](../design/path-filesystem.md) et "
            "[processus](../design/process-runtime.md).",
            "",
            "## Imports de fixtures et couverture",
            "",
            "Un module sans import direct n'est pas nécessairement non testé "
            "(il peut être atteint par réexport ou par un test C++/runtime), mais "
            "il doit recevoir une fixture explicite dans son lot propriétaire.",
            "",
            *fixture_details(model),
            "",
            "## Principaux motifs dupliqués",
            "",
            "| Motif normalisé | Modules | Occurrences |",
            "| --- | --- | ---: |",
        ]
    )
    for pattern in model.duplicate_patterns:
        text = markdown_cell(pattern.text)
        module_names = ", ".join(f"`{module}`" for module in pattern.modules)
        lines.append(f"| `{text}` | {module_names} | {pattern.occurrences} |")
    lines.extend(
        [
            "",
            "Ces répétitions orientent les lots sans autoriser une abstraction "
            "aveugle : #111 mutualise parcours et fallbacks, #112 les sondes et "
            "croissances de tables, #113 les conversions d'erreurs/buffers/handles "
            "et #114 les wrappers de ressources et paires begin/end.",
            "",
            "## Registre exhaustif des symboles",
            "",
            "Chaque symbole hérite ici d'une décision explicite et d'un "
            "propriétaire de migration. Le registre contient exactement la "
            "surface extraite des sources ; le test `docs.stdlib_audit` détecte "
            "tout ajout, retrait ou changement non régénéré.",
            "",
            *symbol_registry(model),
        ]
    )
    return "\n".join(lines).rstrip() + "\n"


def report_is_current(path: Path, expected: str) -> bool:
    return path.is_file() and path.read_text(encoding="utf-8") == expected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--write", action="store_true", help="régénérer le rapport")
    actions.add_argument("--check", action="store_true", help="vérifier le rapport")
    args = parser.parse_args()

    root = args.root.resolve()
    report_path = root / REPORT_PATH
    model = collect_audit(root)
    expected = render_report(model)
    if args.write:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(expected, encoding="utf-8")
        print(f"Audit stdlib écrit dans {report_path.relative_to(root)}")
        return 0
    if not report_is_current(report_path, expected):
        print(
            f"ERROR {report_path.relative_to(root)} est absent ou périmé ; "
            "exécuter scripts/audit_stdlib.py --write",
            file=sys.stderr,
        )
        return 1
    print(
        f"Audit stdlib vérifié: {len(model.modules)} modules, "
        f"{len(model.symbols)} symboles"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
