<span class="chapter-kicker">CHAPITRE 07 / INDUSTRIALISER</span>
# Projets, tests et outils

## Objectifs

- lire `janus.toml` et versionner `janus.lock` ;
- écrire un test exécutable ;
- utiliser formatage, diagnostics et LSP.

## Le manifeste

```toml
[package]
name = "application"
version = "0.1.0"
entry = "src/main.janus"

[dependencies]
outil = { path = "../outil" }
```

Ajoutez `janus.lock` au contrôle de version ; ignorez `target/`. Une dépendance Git reproductible utilise un hash de commit complet.

## Un test est un programme

Créez `tests/basic.janus` :

```janus
def main() : int {
    val result : int = 40 + 2
    if result == 42 {
        return 0
    }
    return 1
}
```

Puis exécutez :

```bash
janus fmt --check
janus check
janus test
janus build --release
```

`janus-lsp` apporte diagnostics, survol, définitions, références, symboles du workspace, complétion et formatage. L’extension VS Code du dépôt le détecte dans l’installation Janus ou le `PATH`.

## Exercice

Écrivez un test qui réussit uniquement si `maximum(7, 12)` retourne `12`.

??? success "Correction"
    ```janus
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

<div class="lesson-nav"><a href="06-collections-iterateurs/">← Collections et itérateurs</a><a href="08-projet-final/">Projet final →</a></div>
