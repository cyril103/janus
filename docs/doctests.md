# Doctests Janus

Statut : contrat proposé pour Janus 0.7.3.

## Blocs exécutables et exemples incomplets

`janus test` découvre les fichiers Markdown de `README.md` et `docs/` dans le
paquet. Un bloc est compilé lorsqu’il porte la directive `doctest` :

````markdown
```janus doctest name=addition
def add(left : int, right : int) : int {
    return left + right
}

def main() : int {
    return add(20, 22) - 42
}
```
````

Le nom est facultatif et doit rester unique dans le document. Un bloc
`janus` sans directive est illustratif et n’est pas exécuté. La directive
`incomplete` (ou `ignore`) rend cette intention explicite :

````markdown
```janus incomplete
val fragment : int = construireLaSuite(...)
```
````

Les sources temporaires sont compilées avec le même contexte que les tests du
paquet : bibliothèque standard, dépendances résolues et modules sous `src/`.
Ainsi, renommer une API importée casse son exemple documentaire.

## Exemples qui doivent échouer

Une erreur attendue indique son code de diagnostic stable :

````markdown
```janus compile_fail=JANA0001 name=valeur-absente
def main() : int {
    return valeurAbsente
}
```
````

Le doctest réussit si la compilation échoue et si au moins un diagnostic porte
exactement ce code. Le texte et la ponctuation du message ne sont pas comparés.
Une compilation réussie ou un autre code fait échouer le test.

## Exécution et filtrage

```bash
janus test                         # tests .janus et doctests du paquet
janus test addition                # filtre chemins, lignes et noms
janus test --doc                   # doctests uniquement
janus test --doc --doc-path guide  # racine Markdown relative au paquet
```

`--doc-path` peut être répété ; dès qu’il est présent, il remplace les chemins
par défaut. Les fichiers sont découverts et exécutés dans un ordre
déterministe. Chaque résultat et chaque erreur indiquent
`document:ligne`, où la ligne est celle du premier contenu du bloc.

Le site officiel utilise ce même chemin d’exécution avec
`janus test --doc --doc-path docs`; son contexte de paquet se trouve sous
`website/`.
