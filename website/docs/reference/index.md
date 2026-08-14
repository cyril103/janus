# Référence Janus 0.12.0

Cette section publie les guides canoniques du dépôt. Ils sont copiés à chaque build : il n’existe donc pas de version éditoriale concurrente dans le site.

<div class="reference-map" markdown>

**Commencer**

:   [Installation et premier projet](generated/getting-started.md) — installer la chaîne complète, créer, compiler et tester un projet.

**Écrire**

:   [Guide du langage](generated/language-guide.md) · [Conversion et parsing de texte](generated/text.md)

**Outiller**

:   [CLI, projets, dépendances et LSP](generated/tooling.md) · [Diagnostics structurés](generated/diagnostics.md) · [Documentation d’API](generated/api-documentation.md) · [Doctests](generated/doctests.md) · [Compiler depuis les sources](generated/development.md)

**Bibliothèque standard**

:   [Guide et doctests par module](generated/stdlib-reference.md) · [Index API généré](stdlib/index.html)

**Auditer**

:   [Inventaire de stabilité 0.8](generated/stability-inventory-0.8.md) · [Surface publique symbolique 0.5.x](generated/public-surface-0.5.json) · [Rapport de préparation 1.0](generated/readiness-1.0.md)

**Migrer**

:   [Passer de Janus 0.5 à 0.8](generated/migration-0.5-to-0.8.md) · [Migration historique 0.5 vers 0.6](generated/migration-0.5-to-0.6.md)

**Explorer**

:   [Graphisme 2D avec raylib 6](generated/graphics.md) · [Contrat de stabilité proposé pour 1.0](generated/stability-contract.md)

</div>

## Référence canonique des opérateurs entiers

`&`, `^` et `|` exigent deux entiers du même type. `<<` et `>>` exigent un
entier à gauche et un `usize` à droite. Le résultat conserve le type gauche ;
`>>` est logique sur un type non signé et arithmétique sur un type signé. Le
compte valide est `0..largeur-1`; sinon la compilation constante diagnostique
l'erreur ou l'exécution panique avant l'instruction de décalage.

Priorité décroissante : `* / %`, `+ -`, `<< >>`, `< <= > >=`, `== !=`, `&`,
`^`, `|`, `&&`, `||`.

!!! warning "Version pré-1.0"
    Cette référence décrit **Janus 0.12.0**. L’inventaire 0.5.x reste publié
    comme source symbolique de l’audit 0.8. Avant la version 1.0, seules les
    gates du candidat s’appliquent ; la compatibilité définitive n’est pas
    encore promise.
