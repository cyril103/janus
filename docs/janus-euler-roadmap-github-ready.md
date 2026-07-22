# Roadmap d’amélioration Janus (format GitHub-ready)

## Milestone 1 — Stabilité de base (2 semaines)

### Goal
Mettre en place une chaîne de validation fiable pour éviter les plantages silencieux pendant les exécutions Project Euler (et autres katas).

### Label suggérés
`area:runtime`, `priority:high`, `type:infra`

---
### Épic 1.1 — Harness de validation
**Issue 1.1.1 — feat(harness): add verifier script for project-euler suite**
- **Body:** Créer `scripts/verify-janus-euler-suite.sh` avec:
  - exécution `janus check`, `janus build`, run binaire
  - mode `--problem N`, `--all`, `--release`
  - timeout (ex: 60s par cas) et timeout global
  - sortie structurée JSON + log lisible en console
- **Acceptance:** la commande retourne un résultat par problème (ok/crash/timeout/mismatch) sans exécution manuelle.

**Issue 1.1.2 — feat(harness): persist structured run results for CI and triage**
- **Body:** Ajouter `target/verify/` (ou dossier équivalent) avec artefacts JSON + logs par run: code retour, durée, message d’erreur (segfault/timeout).
- **Acceptance:** exécutions répétables localement et dans CI, comparaison facile historique.

### Épic 1.2 — Tri compile vs runtime
**Issue 1.2.1 — chore(ci): separate compile and runtime result checks**
- **Body:** Reporter explicitement dans le harness:
  - `compile_ok` (check/build)
  - `run_ok`
  - `segfault`
  - `timeout`
  - `output_mismatch`
- **Acceptance:** aucun run ne peut passer silencieusement en cas de segfault.

**Issue 1.2.2 — test(janus): add minimal per-problem smoke tests (Euler 1..20)**
- **Body:** créer un répertoire de fixtures tests rapides avec cas de petite taille + cas de production pour chaque solution.
- **Acceptance:** un seul problème peut être validé indépendamment.

---
## Milestone 2 — Langage & ergonomie (semaine 3-4)

### Goal
Supprimer les frictions syntaxiques observées en production.

### Labels suggérés
`type:compiler`, `area:language`, `priority:high`

**Issue 2.1.1 — feat(parser): handle top-level declarations ergonomically**
- **Body:** documenter/autoriser (ou diagnostiquer proprement) les constantes top-level de manière prévisible (`val`/`const`), au lieu d’un message brut.
- **Acceptance:** message explicite + guidance de migration (ex: wrapping en fonction).

**Issue 2.1.2 — fix(typecheck): clearer missing-return diagnostics**
- **Body:** améliorer les messages pour fonctions non-unité sans `return` explicite.
- **Acceptance:** message mentionne fonction, type attendu et ligne cible.

**Issue 2.1.3 — feat(docs): canonical output pattern for numbers**
- **Body:** doc + check quickstart indiquant `print(label)` + `println(value)` (éviter concat string/nombre).
- **Acceptance:** exemple validé + docs à jour.

**Issue 2.1.4 — feat(std): add optional diagnostics for high-risk loop patterns**
- **Body:** warning sur boucles à très forte croissance (sans bloquer), utile pour Project Euler.
- **Acceptance:** warning optionnel visible avec flag, incluant ligne de la boucle.

---
## Milestone 3 — Stabilité runtime/perf (semaine 5-7)

### Goal
Éliminer les crashs `exit 139` sur cas de calculs intensifs.

### Labels suggérés
`type:runtime`, `priority:high`, `area:perf`

**Issue 3.1.1 — fix(runtime): add crash telemetry for segfault paths**
- **Body:** enrichir la sortie runtime avec mapping source/stack simplifié quand disponible.
- **Acceptance:** identification du site de crash (ou meilleur signal) dans les logs.

**Issue 3.1.2 — perf(janus): optimize palindrome search strategy**
- **Body:** remplacer la version ascendante de `problem4` par une version descendante avec élargage contrôlé et borne `product <= current_best`.
- **Acceptance:** pas de régression de résultat, meilleure robustesse.

**Issue 3.1.3 — perf(janus): safe mode for heavy numeric loops**
- **Body:** offrir mode d’exécution “safe” avec instrumentation légère, pour isoler rapidement la version minimale stable.
- **Acceptance:** bench de fallback sans segfault sur Euler 1..20.

**Issue 3.1.4 — refactor(janus): remove known-unstable dynamic variants**
- **Body:** retirer les implémentations naïves instables en faveur de versions bornées/élaguées.
- **Acceptance:** mêmes outputs sur cas tests standards.

---
## Milestone 4 — Types numériques & stdlib (semaine 8-10)

### Goal
Rendre Janus robuste sur grands entiers et helpers mathématiques.

### Labels suggérés
`type:compiler`, `area:stdlib`, `area:types`, `priority:medium`

**Issue 4.1.1 — feat(types): stabilize integer model and overflow behavior**
- **Body:** fiabiliser comportement des entiers (limites, overflow) et définir politique claire.
- **Acceptance:** docs décrivant clairement ce qui passe/faille.

**Issue 4.1.2 — feat(stdlib): add `gcd`/`lcm`/`is_prime` helpers**
- **Body:** ajouter API basique pour Project Euler (et algos similaires).
- **Acceptance:** exemples en tests et docs.

**Issue 4.1.3 — feat(stdlib): add integer-factorization helper**
- **Body:** helper de factorisation simple optimisé (pour Euler 3 et suivants).
- **Acceptance:** tests unitaires dédiés + benchmark de base.

**Issue 4.1.4 — chore(problem): replace hardcoded constants in Euler solutions**
- **Body:** retirer les retours constants (p. ex. problématiques instables) dès que runtime est stable.
- **Acceptance:** toutes les solutions 1..20 calculées dynamiquement sans valeurs codées en dur.

---
## Milestone 5 — DX, CI et gestion (semaine 11-12)

### Goal
Améliorer expérience développeur et empêcher les régressions.

### Labels suggérés
`type:dx`, `type:ci`, `priority:low`

**Issue 5.1.1 — feat(cli): normalize janus execution subcommands**
- **Body:** garantir `--help`, sorties d’erreur propres, code retour cohérent pour run/check/build/test.
- **Acceptance:** UX comparable entre modes.

**Issue 5.1.2 — docs: project-euler troubleshooting playbook**
- **Body:** doc anti-segfault: séquence `check/build/run`, patterns de crash, checklists de fallback.
- **Acceptance:** checklist utile prête avant contribution.

**Issue 5.1.3 — ci: gate with euler suite and time budgets**
- **Body:** CI (ou script CI local) avec:
  - `janus check`
  - `janus build`
  - suite Euler 1..20 + budget temps/erreur.
- **Acceptance:** merge bloqué si une régression runtime/timeout.

**Issue 5.1.4 — meta: weekly regression dashboard**
- **Body:** rapport simple des 20 problèmes: status, durée, crashs, mismatch.
- **Acceptance:** tableau lisible, mis à jour automatiquement.

---

## Optional rollout script (création automatique GitHub)

Si tu veux, je peux lancer un script qui crée ces issues/milestones automatiquement (via `gh` ou `curl`) avec:
- owner: `cyril103`
- repo: `janus`
- mapping milestones 1..5 (ordre + due dates)
- labels `type:*`, `area:*`, `priority:*`

## Notes de dépendance (ordre d’exécution)
- `Milestone 1` -> prérequis strict pour toute autre phase.
- `Milestone 2` peut courir en parallèle de 3 sur des modules non dépendants.
- `Milestone 4` dépend des conclusions de `Milestone 3` sur stabilité runtime.
- `Milestone 5` doit intégrer les résultats des précédentes phases.

## Fichier de sortie
- Créé: `/opt/data/repos/janus/docs/janus-euler-roadmap-github-ready.md`