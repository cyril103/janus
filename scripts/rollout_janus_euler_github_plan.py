#!/usr/bin/env python3
"""Create Janus Project Euler roadmap milestones + issues on GitHub.

Usage:
  python3 rollout_janus_euler_github_plan.py --apply

By default, it prints what it would do (dry-run).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass
from datetime import date, timedelta
from typing import Any, Dict, List

import urllib.request
import urllib.error

GITHUB_API = "https://api.github.com"
OWNER = "cyril103"
REPO = "janus"
DOC_PATH = "docs/janus-euler-roadmap-github-ready.md"


@dataclass
class IssueSpec:
    title: str
    body: str
    labels: List[str]
    milestone: str


def now_iso() -> str:
    return f"{date.today()}T00:00:00Z"


def api_request(method: str, token: str, endpoint: str, payload: Dict | None = None):
    url = f"{GITHUB_API}{endpoint}"
    data = None
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "User-Agent": "hermes-github-rollout",
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            body = r.read().decode("utf-8")
            if not body:
                return {}
            return json.loads(body)
    except urllib.error.HTTPError as e:
        text = e.read().decode("utf-8", errors="ignore")
        raise RuntimeError(f"HTTP {e.code} {e.reason} for {url}: {text}") from e


def get_token() -> str:
    token = os.getenv("GITHUB_TOKEN") or os.getenv("GH_TOKEN")
    if not token and os.path.exists("/opt/data/.env"):
        with open("/opt/data/.env", "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("GITHUB_TOKEN="):
                    token = line.split("=", 1)[1].strip().strip('"').strip("'")
                    break
                if not token and line.startswith("GH_TOKEN="):
                    token = line.split("=", 1)[1].strip().strip('"').strip("'")
    if not token:
        raise SystemExit("No GitHub token found (GITHUB_TOKEN or GH_TOKEN).")
    return token


def list_existing_milestones(token: str) -> Dict[str, int]:
    items = api_request("GET", token, f"/repos/{OWNER}/{REPO}/milestones?state=all&per_page=100")
    return {m["title"]: m["number"] for m in items}


def create_or_get_milestone(token: str, title: str, due: str, number_map: Dict[str, int], dry_run: bool) -> int:
    if title in number_map:
        return number_map[title]
    payload = {
        "title": title,
        "state": "open",
        "description": f"Milestone auto-created from {DOC_PATH}",
        "due_on": due,
    }
    if dry_run:
        print(f"[DRY-RUN] create milestone: {title} due={due}")
        return -1
    data = api_request("POST", token, f"/repos/{OWNER}/{REPO}/milestones", payload)
    return data["number"]


def list_labels(token: str) -> Dict[str, bool]:
    items = api_request("GET", token, f"/repos/{OWNER}/{REPO}/labels?per_page=100")
    return {l["name"]: True for l in items}


def ensure_label(token: str, name: str, color: str, dry_run: bool):
    existing = list_labels(token)
    if name in existing:
        return
    payload = {"name": name, "color": color, "description": "Auto-created from Janus Euler roadmap"}
    if dry_run:
        print(f"[DRY-RUN] create label: {name}")
        return
    api_request("POST", token, f"/repos/{OWNER}/{REPO}/labels", payload)

def existing_issue_map(token: str) -> Dict[str, Dict[str, Any]]:
    # fetch open+closed issues (excluding PRs) and keep metadata
    issues: Dict[str, Dict[str, Any]] = {}
    for state in ["open", "closed"]:
        page = 1
        while True:
            items = api_request("GET", token, f"/repos/{OWNER}/{REPO}/issues?state={state}&per_page=100&page={page}")
            if not items:
                break
            for i in items:
                if "pull_request" in i:
                    continue
                issues[i["title"]] = {
                    "number": i["number"],
                    "milestone": i["milestone"]["number"] if i.get("milestone") else None,
                }
            page += 1
            if len(items) < 100:
                break
    return issues


def resolve_milestone(spec: IssueSpec, milestone_map: Dict[str, int]) -> int | None:
    if spec.milestone in milestone_map:
        return milestone_map[spec.milestone]

    # tolerate shorthand values like "Milestone 1"
    if spec.milestone.startswith("Milestone "):
        for title, num in milestone_map.items():
            if title.startswith(spec.milestone):
                return num
    return None


def set_issue_milestone(token: str, number: int, milestone_num: int, dry_run: bool):
    if dry_run:
        print(f"[DRY-RUN] would update issue #{number} milestone -> {milestone_num}")
        return
    api_request("PATCH", token, f"/repos/{OWNER}/{REPO}/issues/{number}", {"milestone": milestone_num})


def create_issue(token: str, spec: IssueSpec, milestone_map: Dict[str, int], existing_issues: Dict[str, Dict[str, Any]], dry_run: bool):
    ms_num = resolve_milestone(spec, milestone_map)
    body = (
        f"{spec.body}\n\n---\n"
        "Source: auto-generated from `docs/janus-euler-roadmap-github-ready.md`\n"
        f"Milestone: {spec.milestone}\n"
    )
    payload = {
        "title": spec.title,
        "body": body,
        "labels": spec.labels,
        "milestone": ms_num,
    }

    if spec.title in existing_issues:
        existing = existing_issues[spec.title]
        existing_milestone = existing["milestone"]
        if ms_num and existing_milestone != ms_num:
            set_issue_milestone(token, existing["number"], ms_num, dry_run)
            print(f"[OK] updated milestone for: {spec.title}")
        else:
            print(f"[SKIP] issue already exists: {spec.title}")
        return

    if dry_run:
        print(f"[DRY-RUN] create issue: {spec.title} -> {spec.milestone}")
        return
    api_request("POST", token, f"/repos/{OWNER}/{REPO}/issues", payload)
    print(f"[OK] created issue: {spec.title}")


def define_plan() -> tuple[List[Dict], List[IssueSpec]]:
    start = date.today()
    milestones = [
        ("Milestone 1 — Stabilité de base", "Milestone 1", 14),
        ("Milestone 2 — Langage & ergonomie", "Milestone 2", 28),
        ("Milestone 3 — Stabilité runtime/perf", "Milestone 3", 49),
        ("Milestone 4 — Types numériques & stdlib", "Milestone 4", 63),
        ("Milestone 5 — DX, CI et gestion", "Milestone 5", 77),
    ]
    milestones = [
        {
            "title": t,
            "tag": tag,
            "due_on": f"{(start + timedelta(days=d)).isoformat()}T00:00:00Z",
            "description": f"Auto-generated roadmap execution window for {t}",
        }
        for t, tag, d in milestones
    ]

    issues = [
        IssueSpec(
            "feat(harness): add verifier script for project-euler suite",
            "## Goal\nMettre en place une chaîne de validation fiable pour Janus Project Euler.\n\n## Definition of Done\n- Script `scripts/verify-janus-euler-suite.sh` implémente check/build/run\n- Modes `--problem`, `--all`, `--release`\n- Timeout configurable (ex: 60s/problème) et timeout global\n- Rapport JSON + log lisible en console",
            ["type:infra", "area:runtime", "priority:high"],
            "Milestone 1",
        ),
        IssueSpec(
            "feat(harness): persist structured run results for CI and triage",
            "## Goal\nStocker les résultats structurés des runs pour faciliter CI et analyse post-mortem.\n\n## Definition of Done\n- Persist d\'artefacts dans un dossier dédié\n- Chaque run capture: code retour, durée, sortie parseable, error flags\n- Exécution répétable avec comparaison historique",
            ["type:infra", "area:runtime", "priority:high"],
            "Milestone 1",
        ),
        IssueSpec(
            "chore(ci): separate compile and runtime result checks",
            "## Goal\nDistinguer explicitement compile-time et runtime dans la pipeline.\n\n## Definition of Done\n- Les rapports montrent clairement: compile_ok / run_ok / segfault / timeout / output_mismatch",
            ["type:ci", "priority:high", "area:runtime"],
            "Milestone 1",
        ),
        IssueSpec(
            "test(janus): add minimal per-problem smoke tests (Euler 1..20)",
            "## Goal\nAjouter des fixtures simples pour valider chaque problème indépendamment.\n\n## Definition of Done\n- Un cas par problème peut être lancé seul\n- Cas court + cas réel de régression\n- Intégration à la checklist de milestone 1",
            ["type:runtime", "priority:high", "area:runtime"],
            "Milestone 1",
        ),
        IssueSpec(
            "feat(parser): handle top-level declarations ergonomically",
            "## Goal\nAméliorer l\'expérience autour des declarations top-level (`val`/`const`).\n\n## Definition of Done\n- Erreur explicite ou support prévu\n- Message de migration clair dans l\'erreur\n- Note de documentation associée",
            ["type:compiler", "area:language", "priority:high"],
            "Milestone 2",
        ),
        IssueSpec(
            "fix(typecheck): clearer missing-return diagnostics",
            "## Goal\nClarifier les diagnostics de retour de fonction manquant.\n\n## Definition of Done\n- Message mentionne fonction, ligne et type attendu\n- Réduction des ambiguïtés de compilation",
            ["type:compiler", "area:language", "priority:high"],
            "Milestone 2",
        ),
        IssueSpec(
            "feat(docs): canonical output pattern for numbers",
            "## Goal\nStandardiser la recommandation d\'affichage pour valeurs numériques.\n\n## Definition of Done\n- Doc de quickstart mise à jour\n- Exemple `print(label)` + `println(value)` validé",
            ["type:dx", "priority:medium", "area:language"],
            "Milestone 2",
        ),
        IssueSpec(
            "feat(std): add optional diagnostics for high-risk loop patterns",
            "## Goal\nAjouter un warning (non bloquant) sur patterns de boucles potentiellement explosives.\n\n## Definition of Done\n- Mode diagnostic activable\n- Warning inclut une indication de ligne\n- Option de désactivation",
            ["type:compiler", "area:language", "priority:high"],
            "Milestone 2",
        ),
        IssueSpec(
            "fix(runtime): add crash telemetry for segfault paths",
            "## Goal\nAméliorer le diagnostic des crashs runtime (ex: exit 139).\n\n## Definition of Done\n- Mapping source/stack simplifié quand disponible\n- Logs plus précis dans les run harness",
            ["type:runtime", "area:runtime", "priority:high"],
            "Milestone 3",
        ),
        IssueSpec(
            "perf(janus): optimize palindrome search strategy",
            "## Goal\nRendre le calcul du problème 4 plus robuste sans régression.\n\n## Definition of Done\n- Remplacement de version ascendante par approche descendante avec prune\n- Mesure de stabilité améliorée",
            ["type:runtime", "area:perf", "priority:high"],
            "Milestone 3",
        ),
        IssueSpec(
            "perf(janus): safe mode for heavy numeric loops",
            "## Goal\nIntroduire un mode d\'exécution safe/diagnostic pour boucles lourdes.\n\n## Definition of Done\n- Isolation des cas problématiques\n- Fallback stable pour Euler 1..20",
            ["type:runtime", "area:perf", "priority:high"],
            "Milestone 3",
        ),
        IssueSpec(
            "refactor(janus): remove known-unstable dynamic variants",
            "## Goal\nÉliminer les implémentations non stables au profit de versions bornées/élaguées.",
            ["type:runtime", "area:perf", "priority:high"],
            "Milestone 3",
        ),
        IssueSpec(
            "feat(types): stabilize integer model and overflow behavior",
            "## Goal\nDéfinir et stabiliser le comportement entier pour les cas algorithmiques.\n\n## Definition of Done\n- Documentation de politique overflow\n- Tests dédiés aux bords",
            ["type:compiler", "area:types", "priority:medium"],
            "Milestone 4",
        ),
        IssueSpec(
            "feat(stdlib): add `gcd`/`lcm`/`is_prime` helpers",
            "## Goal\nAjouter helpers mathématiques essentiels pour travaux numérisés.\n\n## Definition of Done\n- API dans stdlib\n- Cas tests et exemples\n",
            ["type:compiler", "area:stdlib", "priority:medium"],
            "Milestone 4",
        ),
        IssueSpec(
            "feat(stdlib): add integer-factorization helper",
            "## Goal\nAjouter un helper de factorisation simple et utile pour Euler.\n\n## Definition of Done\n- API stable\n- Bench/microbench de base\n- Documentation\n",
            ["type:compiler", "area:stdlib", "priority:medium"],
            "Milestone 4",
        ),
        IssueSpec(
            "chore(problem): replace hardcoded constants in Euler solutions",
            "## Goal\nRetirer les retours constants dès que la stabilité runtime permet le calcul réel.\n\n## Definition of Done\n- Toutes les solutions 1..20 calculent dynamiquement leurs résultats",
            ["type:runtime", "area:runtime", "priority:medium"],
            "Milestone 4",
        ),
        IssueSpec(
            "feat(cli): normalize janus execution subcommands",
            "## Goal\nHarmoniser `--help` et codes retour CLI (check/build/run/test).\n\n## Definition of Done\n- Cohérence UX et codes de sortie\n- Messages d\'erreur plus homogènes",
            ["type:dx", "priority:low", "area:compiler"],
            "Milestone 5",
        ),
        IssueSpec(
            "docs: project-euler troubleshooting playbook",
            "## Goal\nCréer un playbook anti-segfault orienté Project Euler.\n\n## Definition of Done\n- Checklist check/build/run claire\n- Stratégies de fallback documentées",
            ["type:dx", "priority:low", "area:language"],
            "Milestone 5",
        ),
        IssueSpec(
            "ci: gate with euler suite and time budgets",
            "## Goal\nAjouter une gate CI ou script local équivalent pour la suite Euler 1..20.\n\n## Definition of Done\n- `janus check`, `janus build`, suite de validation\n- Budgets de temps/erreurs paramétrables",
            ["type:ci", "type:dx", "priority:low"],
            "Milestone 5",
        ),
        IssueSpec(
            "meta: weekly regression dashboard",
            "## Goal\nRendre la régression facilement visible.\n\n## Definition of Done\n- Rapport hebdo auto des 20 problèmes\n- Statut, durée, crashs, mismatches",
            ["type:dx", "type:ci", "priority:low"],
            "Milestone 5",
        ),
    ]

    return milestones, issues


def validate_labels(token: str, issues: List[IssueSpec], dry_run: bool):
    needed = set()
    for issue in issues:
        needed.update(issue.labels)
    # deterministic simple colors
    palette = {
        "type:infra": "0e8a16",
        "type:compiler": "d73a4a",
        "type:dx": "fbca04",
        "type:ci": "fbca04",
        "type:runtime": "e99695",
        "area:runtime": "0e8a16",
        "area:language": "5319e7",
        "area:compiler": "d73a4a",
        "area:perf": "1d76db",
        "area:stdlib": "5319e7",
        "area:types": "0e8a16",
        "priority:high": "b60205",
        "priority:medium": "f9d0c4",
        "priority:low": "0e8a16",
    }
    for label in sorted(needed):
        color = palette.get(label, "ededed")
        ensure_label(token, label, color, dry_run)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true", help="Actually create milestones/issues (default dry-run)")
    parser.add_argument("--repo", default="janus", help="Repository name (default janus)")
    parser.add_argument("--owner", default="cyril103", help="Repository owner (default cyril103)")
    args = parser.parse_args()

    global OWNER, REPO
    OWNER = args.owner
    REPO = args.repo

    token = get_token()
    milestones, issues = define_plan()

    dry_run = not args.apply

    print(f"Mode: {'APPLY' if args.apply else 'DRY-RUN'}")
    print(f"Target: {OWNER}/{REPO}")
    print(f"Milestones planned: {len(milestones)} | Issues planned: {len(issues)}")

    milestone_map = list_existing_milestones(token)
    for m in milestones:
        num = create_or_get_milestone(token, m["title"], m["due_on"], milestone_map, dry_run)
        if num > 0:
            milestone_map[m["title"]] = num

    # ensure label inventory first
    validate_labels(token, issues, dry_run)

    existing_issues = existing_issue_map(token) if not dry_run else {}
    for issue in issues:
        create_issue(token, issue, milestone_map, existing_issues, dry_run)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
