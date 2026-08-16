<span class="chapter-kicker">CHAPITRE 13 / INDUSTRIALISER</span>
# Projets, tests et outils

## Objectifs

- comprendre `janus.toml`, `janus.lock` et `target/` ;
- utiliser les commandes quotidiennes ;
- écrire tests natifs, doctests et documentation API ;
- profiter du formatter et du serveur de langage.

## Anatomie d’un paquet

```text
application/
├── janus.toml
├── janus.lock
├── src/
│   └── main.janus
└── tests/
    └── basic.janus
```

Le manifeste minimal décrit le paquet et son point d’entrée :

```toml
[package]
name = "application"
version = "0.1.0"
entry = "src/main.janus"

[dependencies]
outil = { path = "../outil" }
```

Versionnez `janus.toml` et `janus.lock`, mais ignorez `target/`. Le lockfile fixe les résolutions reproductibles. Une dépendance Git reproductible référence un hash de commit complet ; le registre v1 apporte recherche, résolution, checksums et provenance.

## Boucle quotidienne

```bash
janus fmt
janus check
janus test
janus run
janus build --release
```

- `fmt` normalise la présentation ; `fmt --check` convient à la CI.
- `check` analyse sans produire l’exécutable final.
- `test` compile et exécute les tests ainsi que les doctests du paquet.
- `run` lance le point d’entrée.
- `build` produit l’artefact natif, en profil debug ou release.

Séparez les arguments du programme des options de Janus avec `--` :

```bash
janus run -- 10 -2 4 8
janus run src/main.janus -- --verbose "texte avec espaces"
```

Chaque argument est transmis tel quel, sans interprétation par un shell, et le
code de sortie du programme devient celui de `janus run`.

`janus clean` retire les artefacts du paquet. `janus new` crée un dossier ; `janus init` initialise le dossier courant. `add` et `remove` modifient les dépendances. `search`, `publish` et les options de registre couvrent l’écosystème de paquets.

## Tests natifs et exécutables

Une fonction publique sans paramètre, non générique et retournant `Unit`
devient un test natif avec la métadonnée `/// @test`. Le runner crée le point
d'entrée et isole chaque fonction dans un processus :

```janus
import std.testing

def maximum(left : int, right : int) : int {
    if left > right {
        return left
    }
    return right
}

/// @test
def maximumKeepsLargestValue() : Unit {
    assertEqual[int](maximum(7, 12), 12)
}
```

`@shouldPanic`, `@ignore` et `@serial` décrivent respectivement une panique
attendue, un test exclu par défaut et un test à exécuter hors du groupe
parallèle. `janus test` propose filtres, nombre de workers, timeouts et rapports
humain, JSON ou JUnit. Les anciens fichiers sous `tests/` qui définissent un
`main` restent exécutables comme un test unique ; une sortie non nulle ou un
crash échoue.

`std.testing.testTemporaryDirectory(false)` crée une ressource propriétaire
dont le destructeur supprime récursivement l'arborescence en best-effort.
`TestTemporaryDirectory.cleanup()` expose le même nettoyage sous forme de
`Result`, observable et idempotent ; utilisez `true` pour conserver le dossier
après le test lors d'un débogage. Enregistrez toujours `defer delete` dès que la
ressource est extraite du `Result`.

## Doctests Markdown

Un bloc `janus` devient exécutable lorsque sa première ligne contient :

```text
// doctest: doctest name=nom-unique
```

`// doctest: compile_fail=CODE` vérifie un diagnostic attendu. `// doctest: incomplete` signale volontairement un fragment illustratif. Le site officiel compile ses exemples avec `janus test --doc` : la documentation reste ainsi alignée sur le langage.

## Documentation API

Écrivez des commentaires `///`, puis générez la documentation :

```bash
janus doc
janus doc --format json
janus doc --stdlib --offline -o site/api
```

Une API publique doit expliquer son contrat de propriété : l’appel observe-t-il, copie-t-il ou consomme-t-il la valeur ? Mentionnez aussi les erreurs, préconditions et ressources retournées.

## Formatter, diagnostics et LSP

`janus fmt` conserve la structure du code et rend les clauses comme `derives`
stables. Les diagnostics possèdent une gravité, un code, une position, des
notes et parfois une suggestion. `--diagnostic-format json` fournit le même
modèle structuré aux outils.

Les avertissements `JANA0002` à `JANA0022` couvrent notamment les fuites,
écrasements de propriétaires, nettoyages incomplets, captures de closures,
casts numériques, emprunts temporaires, cycles et contrats FFI absents. Ne les
corrigez pas en ajoutant mécaniquement `move`, `numericCast` ou un qualificateur :
identifiez d'abord qui possède la valeur et qui doit la nettoyer. La
[référence des diagnostics](../reference/generated/diagnostics.md) donne la
réponse attendue pour chaque code.

L'analyse supplémentaire `--warn-high-growth-loops` est disponible sur
`check`, `build` et `run` pour repérer certaines croissances entières suspectes
dans les boucles.

`janus-lsp` fournit diagnostics, survol, définition, références, symboles du document/workspace, complétion et formatage. Configurez l’extension de l’éditeur pour utiliser le binaire de la même version que le compilateur du projet.

## Identifier la toolchain

`janus`, `janus-lsp` et `janusup` partagent une identité de build. La sortie
humaine convient au diagnostic rapide ; le JSON de schéma 1 expose notamment
la version, la révision, le canal, la cible et LLVM :

```bash
janus --version
janus --version --json
```

Cette identité complète participe au cache incrémental. `janus build
--no-cache` permet de diagnostiquer un écart sans lire ni écrire ce cache. Pour
compiler Janus lui-même, les versions majeures LLVM prises en charge sont 18 à
21 incluses ; CMake refuse explicitement une version hors de cette plage.

L'extension VS Code est produite et vérifiée comme VSIX par la CI. Sa
publication sur la Marketplace reste une étape manuelle du mainteneur : le
dépôt ne contient aucun secret ni automatisme de publication Marketplace.

## Une CI minimale

```bash
janus fmt --check
janus check --locked
janus test --locked
janus build --release --locked
```

`--locked` empêche une résolution implicite différente du lockfile. Pour les performances du compilateur, les options de timings et de trace permettent de distinguer parsing, analyse, génération et linkage.

## Exercice

Écrivez la checklist d’une contribution qui ajoute une fonction publique et son exemple.

??? success "Correction"
    1. Implémenter la fonction et ses tests.
    2. Ajouter les commentaires `///`, `@param` et `@return`.
    3. Ajouter un doctest ou un exemple explicitement incomplet.
    4. Exécuter `janus fmt --check`, `janus check`, `janus test` et `janus doc`.
    5. Versionner le manifeste et le lockfile si les dépendances changent.

<div class="lesson-nav"><a href="../12-graphisme-audio/">← Graphisme 2D et audio</a><a href="../14-reference-mots-cles/">Tous les mots-clés →</a></div>
