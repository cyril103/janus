# RFC — inférence restreinte du retour des fonctions privées

Statut : première tranche additive de l'issue #374. Ce texte ne clôt pas
l'issue : les résultats génériques, propriétaires et empruntés restent ouverts.

## Règles normatives

Cette première tranche autorise l'omission pour une fonction libre
explicitement `private`, dont tous les paramètres ont leur type explicite, qui
n'est ni `extern` ni générique et dont le corps est une seule expression
introduite par `=>`. La passe sait déterminer `Unit` et les scalaires `Copy`
intégrés (`int`, les autres entiers, les flottants, `char` ou `bool`) à partir
des littéraux, paramètres, opérateurs scalaires, conditions dont les deux
branches ont le même type et appels non génériques à des fonctions libres du
même module dont le retour est explicite ou inférable par ces mêmes règles.
`string` n'est pas `Copy` dans le modèle actuel et reste annoté.

Cette liste décrit les expressions effectivement prises en charge, et non
toutes les expressions dont l'analyse complète finirait par produire un type
scalaire. Les champs, globals, `match`, casts intégrés, appels directs aux
builtins tels que `println` et appels importés exigent encore une annotation de
retour. Une expression hors de cette tranche produit le diagnostic demandant
cette annotation ; elle n'autorise ni type inventé ni promotion numérique.

```janus
private def square(x : int) => x * x
private def discard() => unit
```

La visibilité, `pure`, `const`, `tailrec`, les modes des paramètres et toute
provenance restent ceux écrits dans la déclaration. L'inférence ne déduit aucun
effet, droit de receveur, ownership ou provenance. Les méthodes (classes,
traits et extensions), exports, déclarations externes, corps blocs, fonctions
génériques et retours agrégats, pointeurs, fonctions, propriétaires ou
empruntés exigent une annotation explicite. Leur rejet doit désigner la
déclaration ou l'expression et proposer l'annotation.

## Graphe et surcharge

Les dépendances entre retours privés sont résolues comme un graphe, sans
dépendre de l'ordre textuel. Une chaîne peut donc appeler une déclaration
ultérieure. Chaque module possède ses identités habituelles ; l'inférence ne
rend pas une fonction privée visible depuis un autre module.

Le type de retour n'est jamais un critère de sélection d'une surcharge. Les
arguments et les règles de surcharge existantes doivent fournir un candidat
unique ; sinon l'inférence s'arrête avec une demande d'annotation. Aucune
promotion numérique supplémentaire n'est introduite.

Un cycle du graphe de types (`a => b()`, `b => a()`) est rejeté avec une
annotation suggérée. Il se distingue de la récursion d'exécution traversant
une frontière déjà typée : une fonction récursive explicitement annotée (et
respectant notamment la règle `tailrec`) peut être appelée par une fonction
dont le retour est inféré.

## Pipeline, diagnostics et compatibilité

Le frontend conserve dans l'AST le fait que l'annotation était absente. Avant
les consommateurs de signatures, une passe de graphe classe uniquement les
types des expressions admissibles et matérialise un `TypeReference` possédé
par l'AST. Elle n'exécute pas l'analyse des corps et ne touche pas aux états de
move, borrow, pureté ou cleanup. L'analyse sémantique complète demeure unique ;
elle valide ensuite l'expression contre le type matérialisé avec les règles
ordinaires. Aucun pointeur vers un type temporaire n'est conservé.

Un échec de cette passe produit un diagnostic unique et local avant les passes
dépendantes, afin d'éviter les cascades et les crashes. Le LSP utilise la même
analyse pour afficher la signature complétée ; une source incomplète reste un
diagnostic normal.

Avant cette tranche, la grammaire exigeait `:` après `)`. La forme `def f()
=> ...` n'était donc pas un raccourci valide pour `Unit`. Il n'existe aucun
programme valide dont le sens `Unit` change. Les signatures annotées, y compris
`: Unit`, conservent exactement leur comportement et leur ABI.

## Hors périmètre restant de #374

- inférence symbolique des résultats génériques ;
- résultats propriétaires, transferts et agrégats ;
- retours empruntés et calcul de provenance ;
- méthodes et contrats de traits ;
- élargissement de l'ensemble `Copy` au-delà des scalaires intégrés.

Ces extensions devront préserver les contraintes mémoire existantes ; elles ne
peuvent pas être obtenues en relâchant un contrôle de move ou de borrow.
