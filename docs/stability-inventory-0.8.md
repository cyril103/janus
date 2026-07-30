# Inventaire de stabilité 0.8

Statut : audit pré-1.0 du 30 juillet 2026. Cet inventaire décrit le candidat
0.8 ; il ne constitue ni une 1.0 ni une promesse définitive de compatibilité.
`stable-candidate` signifie « proposé pour 1.0 et protégé par les gates 0.8 »,
pas « garanti pour toujours ». `experimental` autorise encore une modification
annoncée dans le changelog.

La source de vérité symbolique de la bibliothèque et de la CLI reste
[l'inventaire public contrôlé](public-surface-0.5.json). Le présent document
classe toutes ses surfaces publiques et les autres frontières du
[contrat proposé](stability-contract.md). `std.hash_probe`, privé
d'implémentation, est exclu de la surface publique.

## Langage

| Surface | Statut | Définition | Décision 0.8 |
| --- | --- | --- | --- |
| `syntax.declarations` | `stable-candidate` | [Guide du langage](language-guide.md) | `val`, `var`, fonctions, classes, structs et enums retenus. |
| `syntax.control-flow` | `stable-candidate` | [Guide du langage](language-guide.md) | `if`, boucles, `match` et sauts retenus. |
| `syntax.generics-traits` | `stable-candidate` | [Guide du langage](language-guide.md) | Génériques, traits, `derives` et fonctions de première classe retenus. |
| `syntax.modules-visibility` | `stable-candidate` | [Guide du langage](language-guide.md) | Modules, imports, `private` et `internal` retenus. |
| `syntax.ownership` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | `move`, `delete`, `defer`, `Option`, `Result` et `?` retenus. |
| `semantics.numeric` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Largeurs, overflow et conversions documentés retenus. |
| `semantics.ownership-cleanup` | `stable-candidate` | [Propriété des conteneurs](design/container-ownership.md) | Déplacement et destruction exactement une fois retenus. |
| `semantics.errors-panics` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Effets et nettoyages retenus ; texte des diagnostics exclu. |
| `semantics.constant-evaluation` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Parité avec l'exécution ou rejet retenue. |

## Projet, résolution et ABI

| Surface | Statut | Définition | Décision 0.8 |
| --- | --- | --- | --- |
| `manifest.package` | `stable-candidate` | [Outillage](tooling.md) | `[package]`, `name`, `version`, `entry` retenus. |
| `manifest.dependencies.path` | `stable-candidate` | [Outillage](tooling.md) | Dépendances locales relatives retenues. |
| `manifest.dependencies.git` | `stable-candidate` | [Outillage](tooling.md) | URL et révision complète retenues. |
| `manifest.dependencies.registry` | `experimental` | [Protocole v1](registry-protocol-v1.md) | Client distant encore jeune. |
| `lockfile.format-v1` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Lecture du format 1 retenue. |
| `lockfile.locked` | `stable-candidate` | [Outillage](tooling.md) | Refus de dérive avec `--locked` retenu. |
| `lockfile.offline` | `stable-candidate` | [Outillage](tooling.md) | Absence d'accès réseau avec `--offline` retenue. |
| `resolver.path` | `stable-candidate` | [Outillage](tooling.md) | Résolution locale et conflits de noms retenus. |
| `resolver.git` | `stable-candidate` | [Outillage](tooling.md) | Résolution par révision immuable retenue. |
| `resolver.registry` | `experimental` | [Protocole v1](registry-protocol-v1.md) | Résolution distante et cache restent expérimentaux. |
| `c-abi.extern-def` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Liaison C native par `extern def` retenue. |
| `c-abi.scalars` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Types scalaires documentés retenus. |
| `c-abi.pointers` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | `Ptr[T]`, `cstr()` et `Unit` retenus. |
| `c-abi.variadics` | `stable-candidate` | [Contrat de stabilité](stability-contract.md) | Promotions C documentées retenues. |
| `protocol.registry-v1` | `stable-candidate` | [Protocole v1](registry-protocol-v1.md) | Objets et règles du protocole versionné retenus. |

## CLI, outils et distribution

| Surface | Statut | Définition | Décision 0.8 |
| --- | --- | --- | --- |
| `cli.new` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.init` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.add` | `stable-candidate` | [Outillage](tooling.md) | Retenue hors registre distant expérimental. |
| `cli.remove` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.search` | `experimental` | [Outillage](tooling.md) | Recherche distante encore expérimentale. |
| `cli.publish` | `experimental` | [Outillage](tooling.md) | Publication distante encore expérimentale. |
| `cli.clean` | `stable-candidate` | [Builds incrémentaux](incremental-builds.md) | Retenue. |
| `cli.check` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.build` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.run` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.test` | `stable-candidate` | [Doctests](doctests.md) | Retenue. |
| `cli.fmt` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.doc` | `stable-candidate` | [Documentation API](api-documentation.md) | Retenue. |
| `cli.--help` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `cli.--version` | `stable-candidate` | [Outillage](tooling.md) | Retenue. |
| `package.archives` | `stable-candidate` | [Guide de développement](development.md) | Archives tier-1 autonomes et checksums retenus. |
| `package.janusup` | `stable-candidate` | [Outillage](tooling.md) | Installation et canaux retenus. |
| `tooling.janus-lsp` | `experimental` | [Outillage](tooling.md) | Protocole interne et capacités peuvent évoluer. |
| `tooling.vscode` | `experimental` | [Outillage](tooling.md) | UX et matrice de capacités peuvent évoluer. |

## Bibliothèque standard

Le statut porte sur tous les symboles publics et signatures de chaque module
énumérés dans la source de vérité JSON ; le contrôle de dérive refuse un
symbole source non inventorié.

| Surface | Statut | Définition | Décision 0.8 |
| --- | --- | --- | --- |
| `std.array` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.array_builder` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.builder` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.c` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.graphics` | `experimental` | [Graphismes](graphics.md) | Famille encore expérimentale. |
| `std.graphics.audio` | `experimental` | [Graphismes](graphics.md) | Retenue comme expérimentale. |
| `std.graphics.drawing` | `experimental` | [Graphismes](graphics.md) | Retenue comme expérimentale. |
| `std.graphics.input` | `experimental` | [Graphismes](graphics.md) | Retenue comme expérimentale. |
| `std.graphics.resources` | `experimental` | [Graphismes](graphics.md) | Retenue comme expérimentale. |
| `std.graphics.types` | `experimental` | [Graphismes](graphics.md) | Retenue comme expérimentale. |
| `std.fs` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.hashing` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.hashmap` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.hashset` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.io` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.iterator` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.math` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.option` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.path` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.random` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.process` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.range` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.result` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.system` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.text` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.time` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |
| `std.wall_time` | `stable-candidate` | [Référence stdlib](stdlib-reference.md) | Retenue. |

## Preuves de gel

- La [migration 0.5 vers 0.8](migration-0.5-to-0.8.md) rassemble les changements
  annoncés depuis 0.5.
- Les surfaces expérimentales sont toutes reprises dans les
  [limites connues](known-limitations-0.8.md).
- Le [rapport de préparation 1.0](readiness-1.0.md) rend une décision explicite.
- La CI tier-1 construit, teste les doctests, produit et teste les archives ;
  elle confronte aussi les fixtures au dernier Janus publié et au candidat.
- Les quatre corpus de fuzzing versionnés sont exécutés sous ASan et UBSan
  pendant au moins 3 600 secondes chacun par la campagne planifiée.
