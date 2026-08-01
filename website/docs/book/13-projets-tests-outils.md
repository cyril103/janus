<span class="chapter-kicker">CHAPITRE 13 / INDUSTRIALISER</span>
# Projets, tests et outils

## Objectifs

- comprendre `janus.toml`, `janus.lock` et `target/` ;
- utiliser les commandes quotidiennes ;
- écrire tests, doctests et documentation API ;
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

`janus clean` retire les artefacts du paquet. `janus new` crée un dossier ; `janus init` initialise le dossier courant. `add` et `remove` modifient les dépendances. `search`, `publish` et les options de registre couvrent l’écosystème de paquets.

## Tests exécutables

Un fichier sous `tests/` est un programme dont `main` retourne `0` en cas de succès :

```janus
// doctest: doctest name=project-test
def maximum(left : int, right : int) : int {
    if left > right {
        return left
    }
    return right
}

def main() : int {
    if maximum(7, 12) == 12 {
        return 0
    }
    return 1
}
```

Cette convention teste aussi bien une fonction pure qu’une intégration avec le runtime. Une sortie non nulle ou un crash échoue.

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

`janus fmt` conserve la structure du code et rend les clauses comme `derives` stables. Les diagnostics possèdent des codes et des emplacements adaptés aux éditeurs.

`janus-lsp` fournit diagnostics, survol, définition, références, symboles du document/workspace, complétion et formatage. Configurez l’extension de l’éditeur pour utiliser le binaire de la même version que le compilateur du projet.

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
