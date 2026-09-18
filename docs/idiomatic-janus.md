# Écrire du Janus idiomatique

Ce guide définit une convention de présentation pour du code Janus courant.
Il complète le [guide du langage](language-guide.md) sans modifier la grammaire,
le typage ni le modèle de propriété. Le
[corpus exécutable](../examples/idiomatic/README.md) publie, pour chacun des six
cas ci-dessous, une forme explicite pédagogique et une forme idiomatique.

Le but est de rendre l'intention visible. Ce n'est pas un concours de nombre de
lignes : une annotation qui explique un contrat, améliore un diagnostic ou
lève une ambiguïté reste utile.

## Convention

- Employer un corps `=>` pour une fonction réduite à une expression. Garder un
  bloc dès qu'il faut nommer des étapes, enregistrer un nettoyage ou traiter
  plusieurs chemins de contrôle.
- Laisser le compilateur inférer le type d'une variable locale lorsque son
  initialiseur le détermine sans ambiguïté. Annoter les frontières publiques,
  les valeurs vides ou contextuelles et les types qui documentent une décision.
- Laisser le type attendu inférer les paramètres simples d'une lambda. Écrire
  `borrow` ou `borrow var` quand ce mode fait partie de son contrat ; ce ne sont
  pas des annotations décoratives.
- Conserver chaque `move` requis. Pour une ressource locale immuable éligible,
  préférer `using val` qui attache explicitement son nettoyage à la liaison.
  Sinon, placer `defer delete resource` immédiatement
  après l'acquisition d'une ressource qui reste locale. Si une API consomme la
  ressource, lui transférer explicitement la valeur et ne pas programmer un
  second nettoyage.
- Préférer les combinateurs de collection lorsqu'ils expriment directement la
  transformation. Une boucle reste préférable lorsque l'ordre, les sorties
  anticipées ou les effets sont l'information principale.
- Pour `Result` et `Option`, traiter les branches au point où une récupération
  est possible ; propager ou traduire l'erreur à une frontière qui possède le
  contexte nécessaire.

## Ce qui existe aujourd'hui

| Catégorie | Disponible et compilé aujourd'hui | Convention de ce guide | Futur éventuel, non utilisable ici |
| --- | --- | --- | --- |
| Fonctions | Corps bloc et corps expression `=>` | `=>` pour une expression, bloc pour contrôle et cleanup | Toute nouvelle forme implicite de retour |
| Locales | `val`/`var` avec inférence depuis l'initialiseur | Omettre les types locaux évidents | Inférence des paramètres ou retours publics |
| Lambdas | Paramètres inférés depuis un type attendu, corps `=>` ou bloc | Garder les modes d'emprunt significatifs | Placeholders ou syntaxe abrégée non spécifiée |
| Propriété | `move`, `borrow`, `borrow var`, `delete`, `defer`, `using val` | Rendre transfert et nettoyage visibles au point d'effet | Move implicite ou ramasse-miettes |
| Collections | Méthodes `map`, `filter`, `fold` et itérateurs | Composer les opérations quand elles portent l'intention | Compréhensions ou nouveau sucre de pipeline |
| Erreurs | `Option`, `Result`, `match` et `?` | Conserver une branche explicite quand elle ajoute du contexte | Exceptions implicites |

La troisième colonne décrit des choix de style. La dernière ne promet aucune
fonctionnalité : une proposition doit suivre le processus RFC avant de devenir
de la syntaxe Janus.

## Les six scénarios de référence

Les liens « explicite » et « idiomatique » ci-dessous désignent les fichiers
réellement compilés et exécutés par CTest. Chaque paire est comparée à une
sortie canonique indépendante, et non seulement à l'autre variante.

| Scénario | Forme explicite | Forme idiomatique | Ce qui disparaît | Ce qui reste explicite |
| --- | --- | --- | --- | --- |
| Calcul pur | [`pure_calculation_explicit.janus`](../examples/idiomatic/pure_calculation_explicit.janus) | [`pure_calculation_idiomatic.janus`](../examples/idiomatic/pure_calculation_idiomatic.janus) | Retours et types locaux répétitifs | Types des paramètres et du retour public, pureté |
| Fichier sûr | [`safe_file_explicit.janus`](../examples/idiomatic/safe_file_explicit.janus) | [`safe_file_idiomatic.janus`](../examples/idiomatic/safe_file_idiomatic.janus) | Types locaux déjà déterminés | `Result`, transfert du résultat et `defer delete file` |
| Collections | [`collections_explicit.janus`](../examples/idiomatic/collections_explicit.janus) | [`collections_idiomatic.janus`](../examples/idiomatic/collections_idiomatic.janus) | Types de temporaires, `[int]` de `fold` et lignes `defer` séparées | `map[int]` encore nécessaire et nettoyage de chaque tableau via `using val` |
| Callback avec état | [`stateful_callback_explicit.janus`](../examples/idiomatic/stateful_callback_explicit.janus) | [`stateful_callback_idiomatic.janus`](../examples/idiomatic/stateful_callback_idiomatic.janus) | Annotations des scalaires et corps simple | Capacité `FnMut` et transfert de la closure |
| API propriétaire | [`owning_api_explicit.janus`](../examples/idiomatic/owning_api_explicit.janus) | [`owning_api_idiomatic.janus`](../examples/idiomatic/owning_api_idiomatic.janus) | Types locaux déductibles et corps d'observation | `borrow`, `move` et destruction exactement une fois |
| CLI | [`cli_explicit.janus`](../examples/idiomatic/cli_explicit.janus) | [`cli_idiomatic.janus`](../examples/idiomatic/cli_idiomatic.janus) | Types des arguments extraits | Validation, sortie d'usage et codes de retour |

### Comparaison qualitative

Dans le calcul pur et la CLI, l'allègement porte surtout sur des répétitions :
le type est déjà fixé par l'appel ou l'initialiseur. Dans les collections et le
callback, le contexte donne les types des paramètres de lambda ; la capacité
de mutation demeure néanmoins écrite à la frontière qui accepte la callback.
Dans ce scénario, la closure modifie sa copie capturée du compteur : la locale
`running` extérieure reste inchangée. `scoped` interdit l'échappement ; il ne
transforme pas cette capture en référence vers la variable extérieure.

Les scénarios fichier sûr et API propriétaire ne cherchent pas la forme la
plus courte. Leurs opérations importantes sont précisément les `move`,
`borrow`, `delete` et `defer`. La variante idiomatique retire du bruit autour
de ces opérations, sans supprimer leurs obligations. Le cas collections
emploie `using val` : le nettoyage est attaché à la liaison, pas rendu implicite
pour toutes les `val`. `fold` infère son type depuis la graine ; `map[int]`
conserve son argument explicite car le type résultat du callback ne suffit
pas actuellement à contraindre ce paramètre générique. Cette différence qualitative est
plus utile qu'un décompte de lignes : elle permet à la relecture de se
concentrer sur l'autorité, la durée de vie et les chemins d'erreur.

## Contrats de propriété et erreurs conservées

Une forme idiomatique n'autorise jamais l'usage d'une valeur après transfert :
[`ownership_move_after_transfer.janus`](../tests/fixtures/idiomatic/ownership_move_after_transfer.janus)
est un test négatif et doit produire le diagnostic indiquant que `ticket` est
utilisé avant initialisation après `move`.

Un emprunt ne devient pas propriétaire par concision :
[`ownership_delete_borrow.janus`](../tests/fixtures/idiomatic/ownership_delete_borrow.janus)
doit produire le diagnostic indiquant qu'une valeur empruntée ne peut pas être
supprimée. Ces diagnostics sont vérifiés en CI ; les fichiers ne sont donc pas
des pseudocodes.

## Validation et relecture

Le test `docs.idiomatic.*` compile les deux formes de chaque scénario, lie puis
exécute les binaires liés au runtime AddressSanitizer dans des dossiers temporaires
isolés. Cela fournit les interceptions d'allocation/libération et, sous Linux,
la détection de fuites ; ce harnais n'instrumente pas tous les accès mémoire
du code LLVM Janus et ne prouve donc pas l'absence de débordements ou d'accès
après libération. Les tests négatifs exigent le code de rejet sémantique `1`,
pas un crash ou un timeout accompagné du bon message.
Le scénario fichier reçoit un fichier UTF-8 créé par le harnais et la
CLI reçoit deux arguments séparés, dont un contenant une espace. Compilation,
édition de liens et exécution ont chacune une limite de temps. Les sorties
attendues versionnées incluent les valeurs, les nettoyages et la fin de ligne.

La réussite mécanique ne décide pas seule du style. En relecture, vérifier que
l'intention se lit sans remonter inutilement les types, que les erreurs gardent
leur contexte et que chaque transfert, emprunt et cleanup demeure localement
visible.

Les branches d’erreur fichier et CLI sont des exemples lisibles, mais cette
campagne ne teste que leurs chemins heureux. Les sorties ne tracent pas tous
les destructeurs ; ne pas les interpréter comme une preuve exhaustive de
nettoyage. La vérification locale est Linux ; la matrice CI teste les autres
plateformes.
