# Le Book Janus

Ce livre couvre le langage Janus **0.22.0**, de la première fonction au modèle de propriété, aux dérivations, à la bibliothèque standard et au graphisme 2D. Il se lit dans l’ordre pour apprendre, ou par chapitre pour retrouver un concept précis.

!!! info "Nouveautés 0.11 et 0.12"
    Le parcours inclut maintenant les littéraux entiers préfixés, les motifs
    littéraux et gardes de `match`, les littéraux et fabriques de tableaux, le
    tri stable, les tests natifs, les arguments de `janus run` et le nettoyage
    récursif des répertoires. Le chapitre sur les outils précise aussi
    l’identité JSON de la toolchain et la plage LLVM 18 à 21.

## Parcours

1. **Premiers pas** — installer, créer, compiler et exécuter.
2. **Valeurs et types** — liaisons, primitifs, littéraux, casts et opérateurs.
3. **Contrôle et fonctions** — `def`, retours, conditions et boucles.
4. **Modéliser les données** — structs, classes, enums et `match`.
5. **Erreurs et propriété** — `Option`, `Result` et l’opérateur `?`.
6. **Collections et itérateurs** — tableaux, tables et pipelines paresseux.
7. **Génériques et closures** — paramètres de types et fonctions de première classe.
8. **Traits et dérivations** — contrats, `extends`, `Copy`, `Equality`, `Hashing`, `Debug`.
9. **Propriété avancée** — `new`, `move`, `borrow`, `consume`, `delete`, `defer`, destructeurs.
10. **Modules, visibilité et C** — `module`, `import`, `private`, `internal`, `extern`.
11. **Bibliothèque standard** — texte, fichiers, processus, temps et hasard.
12. **Graphisme 2D et audio** — fenêtre, dessin, images, textures, entrées et son.
13. **Projets, tests et outils** — manifeste, paquets, documentation et LSP.
14. **Tous les mots-clés** — index exhaustif des 35 mots-clés réservés et des qualificateurs contextuels.
15. **Projet final** — assembler les concepts dans une application native.

Chaque chapitre contient des objectifs, des exemples, un exercice et une correction repliable. Les exemples marqués comme doctests sont compilés par le compilateur Janus lors de la validation du site.

## Comment utiliser ce livre

- Si vous débutez, suivez les chapitres 1 à 6 avant de choisir une spécialité.
- Si la propriété vous bloque, lisez les chapitres 5 puis 9 et le [tutoriel `move` / `borrow` / `consume`](../tutorials/propriete-move-consume.md).
- Si vous cherchez `derives Copy, Debug`, allez au [chapitre 8](08-traits-derivations.md) puis au [tutoriel consacré aux dérivations](../tutorials/derives-copy-debug.md).
- Pour une signature précise de la stdlib, utilisez l’[index API généré](../reference/stdlib/index.html).

!!! warning "Langage expérimental"
    Avant Janus 1.0, la syntaxe, la bibliothèque standard et le format des paquets peuvent évoluer. Le [contrat de stabilité](../reference/generated/stability-contract.md) décrit la cible 1.0, pas une garantie rétroactive pour 0.x.

[Commencer le chapitre 1 →](01-premiers-pas.md){ .md-button .md-button--primary }
