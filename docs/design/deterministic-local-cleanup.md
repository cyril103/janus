# RFC — nettoyage déterministe des propriétaires locaux

Statut : **étude pour l'issue #376, non acceptée et non implémentée**. Ce
document précède toute modification incompatible. Les règles décrites comme
**RECOMMANDATION À APPROUVER** ne font pas partie du langage actuel. L'utilisateur
n'a accepté ni nouvelle édition, ni sémantique globale des `val`.

## Résumé et décision demandée

Janus possède déjà le mécanisme nécessaire sous une forme explicite : `using
val`. Après une initialisation réussie, cette déclaration enregistre un
`delete` conditionnel dans la pile LIFO commune aux `defer`. Un `move` ou un
`delete` explicite désarme ce nettoyage. L'analyse des emprunts, les sorties de
bloc, `?` et le déroulement des paniques utilisent déjà ce modèle.

**RECOMMANDATION À APPROUVER :** autoriser un prototype isolé qui applique
exactement ce comportement aux seules `val` locales immuables possédantes déjà
acceptées par `using val`, sous un opt-in expérimental de compilation. Le
prototype comparerait une source témoin en `using val` et sa variante opt-in ;
il ne figerait ni syntaxe permanente ni nouvelle édition. Sans approbation, le
langage reste inchangé et `using val` demeure la forme recommandée.

Cette recommandation exclut les `var`, paramètres, champs, globales, captures,
propriétés partielles et propriétaires traversant directement une frontière
FFI. Elle n'ajoute ni GC, ni comptage de références, ni copie ou `move`
implicite. Elle ne promet aucun nettoyage après `abort`, arrêt brutal, perte du
processus ou sortie étrangère qui contourne `janus_panic`.

## Vocabulaire de statut

- **VÉRIFIÉ EXISTANT** : établi sur la révision `efca644` par les sources, le
  guide et/ou les tests cités dans la section « Preuves ».
- **RECOMMANDATION À APPROUVER** : choix prospectif ; aucune implémentation ne
  doit être déduite de ce document.
- **INCONNU / À MESURER** : le dépôt courant ne fournit pas la preuve requise.
- **REPORTÉ** : hors du premier prototype, même si une généralisation devra le
  résoudre.

## Contrat existant de `using val`

### Transitions d'une liaison

| État avant | Événement | État et comportement **VÉRIFIÉ EXISTANT** |
|---|---|---|
| Initialisation non commencée | entrée dans la déclaration | aucune obligation attachée à la liaison |
| Initialisation en cours | construction de temporaires | la liaison n'est pas armée ; les temporaires déjà construits gardent leurs propres nettoyages |
| Initialisation en cours | `?` résiduel ou panique | aucun nettoyage de la liaison incomplète ; nettoyage des temporaires et obligations antérieures |
| Initialisation réussie | stockage de la valeur | armement immédiat d'un `delete` automatique |
| Initialisée et armée | `move r` valide | source désarmée et invalidée ; la destination assume la propriété selon sa propre forme |
| Initialisée et armée | `delete r` valide | désarmement avant l'appel du destructeur, puis destruction anticipée unique |
| Initialisée et armée | sortie de portée | destruction unique à sa position dans la pile LIFO commune |
| Désarmée | lecture, second `move` ou second `delete` | rejet `JANA0044` pour une liaison `using` |
| Initialisée | transfert/destruction avec emprunt ou lecture différée incompatible | rejet par l'analyse d'emprunts ; aucun contournement dynamique |

Le parseur insère actuellement, juste après la déclaration, un
`DeferStatement(DeleteStatement(...), is_automatic=true)`. Le backend évalue
l'initialiseur et stocke la valeur avant de créer et armer le bit
`using.armed`. Au nettoyage, il teste ce bit ; l'opération de destruction le
désarme avant d'exécuter le cleanup propriétaire. Ces détails expliquent les
résultats observés, mais l'intention sémantique à préserver est « obligation
conditionnelle, enregistrée après réussite », pas une disposition précise de
l'IR.

### Sorties et contrôle de flux

| Situation | **VÉRIFIÉ EXISTANT** | Preuve principale |
|---|---|---|
| fin normale de bloc | nettoie les obligations actives, portée interne d'abord | `using_val.janus` |
| `return` | nettoie avant le retour | fonctions `normal` et `early` |
| branche | une destruction anticipée sur le chemin pris désarme le cleanup ; l'autre chemin conserve le cleanup | fonction `branch` |
| boucle | chaque itération a sa portée ; `continue` et `break` nettoient l'itération | boucle de `using_val.janus` |
| `?` | la branche résiduelle nettoie avant de retourner ; la branche succès transfère explicitement le payload | `using_val_try.janus` |
| panique propagée | nettoie les cadres Janus en LIFO | `using_val_panic.janus` et tests de panique ciblés |
| panique pendant une initialisation | ne nettoie pas la liaison absente ; nettoie les préfixes/temporaires construits selon leur contrat | `using_val_partial_panic.janus`, fixtures `named_struct_*cleanup` |
| panique d'un destructeur | l'action est retirée avant exécution ; une seconde panique est ajoutée au diagnostic et les cleanups locaux restants continuent | `using_val_delete_panic.janus`, `destructor_panic_cleanup.janus` |

Les actions `defer` et `using val` partagent une pile LIFO commune. Par
exemple `using first`, `defer println(2)`, `using last` produit `last`, `2`,
`first`. Le cleanup récursif d'un agrégat s'exécute à l'intérieur de son action.
Une destruction explicite est anticipée au point du `delete` ; sa case
automatique ultérieure devient inerte, sans déplacer les autres actions.

Cela ne définit pas un ordre global inverse de construction de toutes les
valeurs : temporaires et préfixes de construction suivent leur contrat propre.
Le backend traite les actions différées d'une portée avant ses valeurs
possédées intermédiaires. Ainsi `using_val_partial_panic.janus` construit la
garde `Resource(1)`, puis l'argument `Resource(2)` avant une panique ; sa trace
vérifiée est **1 puis 2**, sans destruction de `Pair` incomplet (pas de `99`).
Le prototype doit préserver cet ordre existant, et non fusionner ces mécanismes
en une nouvelle pile chronologique.

### Emprunts, effets et pureté

L'ordre LIFO est visible par l'analyse des durées de vie. Un `defer` qui lit un
emprunt doit s'exécuter avant la destruction de sa source. Déplacer ou détruire
une source encore empruntée, ou référencée par une action différée, est refusé.
Une closure ne peut pas faire échapper une ressource locale par capture
empruntée ; un transfert possédant reste explicite.

La destruction n'est pas un effet fictivement pur. `delete`, `defer delete` et
les cleanups normaux, anticipés ou de panique ajoutent le destructeur et les
cleanups transitifs au graphe d'effets. Une `pure def` peut nettoyer une valeur
locale seulement si ce cleanup est lui-même pur. **RECOMMANDATION À
APPROUVER :** le prototype doit réutiliser cette validation sans exemption liée
à l'implicite ; une `val` dont le destructeur a un effet interdit doit faire
rejeter la `pure def` comme la `using val` équivalente.

### Éligibilité réelle, pas heuristique par domaine

| Catégorie | **VÉRIFIÉ EXISTANT** |
|---|---|
| instance de classe propriétaire | éligible si `delete` est valide ; son destructeur et la libération sont le cleanup |
| struct/enum propriétaire | éligible si l'analyse `aggregate_owns_value` détecte un contenu possédé ; cleanup récursif |
| collection propriétaire | éligible par son type propriétaire, pas parce qu'elle est une « collection » ; `Array` est exercé par le corpus idiomatique |
| ressource fichier | une classe comme `FileData` ou `SystemFile` est éligible selon le même contrat de type ; un chemin, un handle emprunté ou un résultat non extrait ne l'est pas par catégorie |
| valeur fonction/closure | `delete` est admis pour une valeur fonction ; le cleanup dépend de la propriété de son environnement. Une closure sans environnement possédé n'acquiert pas artificiellement une ressource |
| valeur `Copy`, scalaire ou struct sans contenu possédé | refusée par `using val` avec `JANA0043`; employer `val` |
| emprunt/pointeur emprunté | non propriétaire, donc non éligible au nettoyage possédant |

L'éligibilité ne doit donc pas être inférée du nom d'une API ou de la présence
d'une allocation apparente. **RECOMMANDATION À APPROUVER :** prendre exactement
le prédicat sémantique déjà employé par `using val` après substitution des
génériques, y compris pour les agrégats et closures.

### Globales et limites runtime

Les globales possédantes ont un cycle distinct déjà implémenté. Le backend
finalise, en ordre inverse, uniquement les globales dont l'initialisation est
achevée et protège le finaliseur contre une seconde entrée. Le diagnostic
interdit leur `delete`/`defer delete` local en rappelant qu'elles sont détruites
automatiquement. **RECOMMANDATION À APPROUVER :** l'opt-in local ne doit ni
sélectionner une globale ni ajouter un second enregistrement.

Une panique Janus termine actuellement par `abort`, après les cleanups locaux
et les finaliseurs globaux applicables. Une panique dans un finaliseur global
interrompt les finaliseurs globaux suivants. Une exception C++, un `longjmp`,
la terminaison étrangère d'un thread, `kill` ou un crash ne sont pas couverts.
Le prototype ne doit élargir aucune de ces garanties.

## Écart ergonomique étudié

Aujourd'hui deux liaisons de même type peuvent différer uniquement par le mot
`using` :

```janus
using val values = new Array[int](4) // langage actuel : cleanup attaché
val other = new Array[int](4)        // langage actuel : cleanup non attaché
```

L'étude demande si un contexte explicitement activé peut traiter la seconde
comme la première. Le pseudo-code suivant décrit seulement l'intention ; il
est **EXPLICITEMENT NON COMPILABLE comme proposition de la RFC** et ne choisit
aucune syntaxe d'opt-in :

```text
PSEUDOCODE NON COMPILABLE
module compilé avec EXPERIMENTAL_DETERMINISTIC_LOCALS

val resource = new Resource(7)
use(resource)
// obligation identique à celle de `using val resource = ...`
```

Ce n'est ni une destruction automatique de toute valeur, ni une inférence de
propriété, ni un remplacement de `move`, des emprunts ou de `delete`.

## Options comparées

| Option | Compatibilité et valeur expérimentale | Conclusion |
|---|---|---|
| maintenir `using val` | aucun changement ; intention locale visible ; répétition du mot-clé | solution actuelle sûre et repli obligatoire |
| mieux exploiter `using val` (guide, diagnostics, migration mécanique) | additive, immédiatement disponible ; ne répond pas entièrement au souhait d'un mode moins verbeux | à poursuivre indépendamment |
| étendre la syntaxe `using` à d'autres liaisons | explicite mais ouvre immédiatement `var`, paramètres/champs ou réaffectation et leurs politiques non résolues | **REPORTÉ** |
| opt-in expérimental borné | comparaison contrôlée avec la sémantique `using val`, sans changer les sources ordinaires | **RECOMMANDATION À APPROUVER** |
| nouvelle édition | frontière source claire, mais décision durable, migration et écosystème prématurés avant données | non recommandée à ce stade ; aucune édition nouvelle n'a été approuvée |
| bascule par défaut | change silencieusement effets, ordre, diagnostics et potentiellement sorties de programmes valides | non acceptable sans RFC acceptée, migration et décision explicite ultérieure |

## Protocole du prototype proposé

Tout ce chapitre est une **RECOMMANDATION À APPROUVER**, pas une spécification
implémentée.

### Périmètre

Le prototype accepte seulement une `val` :

1. locale à un corps de fonction ou de closure ;
2. immuable, avec initialiseur réussi avant armement ;
3. dont le type concret satisfait aujourd'hui l'éligibilité de `using val` ;
4. dont les effets de cleanup passent les validations ordinaires ;
5. qui n'est ni paramètre, champ, globale, capture importée, ni place partielle.

Son modèle de transition, ses diagnostics de move/delete/emprunt, sa pile LIFO,
son unwind et son cleanup agrégé doivent être ceux de la `using val`
équivalente. Le prototype ne doit pas inventer un nouvel attribut permanent
dans la source.

### Frontière de l'opt-in à décider

| Frontière | Avantage | Risque / coût de validation |
|---|---|---|
| module source | intention au plus près du code | exige une syntaxe ou métadonnée persistante, se propage aux imports et peut figer prématurément le langage |
| unité/paquet dans le manifeste | reproductible et partageable | doit définir les modules inclus, dépendances path/git/registry et héritage éventuel |
| option CLI expérimentale | prototype facile à retirer et A/B simple | risque de build non reproductible si le manifeste/cache n'enregistre pas l'option ; driver et LSP doivent recevoir la même valeur |

**RECOMMANDATION À APPROUVER :** commencer par une option CLI explicitement
expérimentale, refusée pour une publication stable si elle n'est pas aussi
matérialisée dans l'identité du build du paquet. Avant même le prototype, la
décision doit préciser :

- si l'option s'applique seulement aux modules sources de l'unité racine ;
- que les dépendances précompilées ou sources conservent leur propre mode et ne
  changent pas silencieusement ;
- qu'une interface exportée ne change pas d'ABI et n'encode pas de nouvelle
  ownership implicite ; seuls les corps locaux sont concernés ;
- que le mode entre dans la clé du cache de compilation et l'instantané du
  graphe, afin qu'un objet produit sans le mode ne soit jamais réutilisé avec ;
- que `janus`, `janusc`, tests/doc-tests et le LSP utilisent la même option ou
  signalent clairement leur divergence.

Le coût driver/LSP est **INCONNU / À MESURER**. Il inclut le transport de
l'option, l'invalidation, les diagnostics/quick-fixes et deux analyses
comparables. Aucun coût nul n'est affirmé.

### Oracle différentiel et corpus

Pour chaque cas, compiler et exécuter deux programmes indépendants : témoin
avec `using val`, candidat avec `val` sous opt-in. Comparer sortie, code de
retour, diagnostics, nombre et ordre des destructeurs. Ne pas remplacer cette
comparaison par une inspection de l'IR seule.

Le corpus minimal doit couvrir : fin normale, blocs imbriqués, deux branches,
boucles complètes, `break`, `continue`, `return`, `?`, panique d'un appel,
panique d'un destructeur, initialisation interrompue, `move`, `delete`
anticipé, emprunt vivant, lecture différée, double `delete` et usage après
move. Les fixtures `using_val*.janus` fournissent déjà la majorité de l'oracle
témoin et doivent être réutilisées.

L'instrumentation future peut compter les entrées de destructeurs dans les
programmes du corpus et constater exactement une entrée par propriétaire
construit non transféré. Cela prouve ce corpus seulement, pas tous les
programmes. Les sanitizers du harnais détectent certaines erreurs runtime ; ils
ne constituent pas une instrumentation IR. Inversement, compter des appels
dans l'IR ne prouve pas quels chemins runtime les exécutent.

## Compatibilité et migration

**VÉRIFIÉ EXISTANT :** sans opt-in, les `val`, `using val`, `delete` et `defer`
gardent leur sens actuel. Le prototype recommandé doit conserver cette
propriété.

Sous un futur mode accepté, ajouter un cleanup peut modifier l'ordre d'effets,
faire rejeter une `pure def`, révéler un double nettoyage manuel ou changer une
sortie observable. La compatibilité source signifie ici qu'un programme est
encore accepté ; elle ne suffit pas à garantir le même comportement. La
compatibilité binaire et l'ABI des signatures peuvent rester intactes pour des
locaux, mais le code généré et ses effets changent : cela reste **À VALIDER**
sur les frontières exportées, callbacks et dépendances mixtes.

La migration doit d'abord produire des diagnostics, pas réécrire sans preuve :

- `delete x` immédiatement anticipé peut être conservé ; il exprime une durée
  de vie plus courte et désarme le cleanup ;
- `defer delete x` exactement équivalent peut devenir redondant, mais seulement
  si l'analyse prouve qu'il vise la même liaison, la même portée **et préserve
  sa position dans l'ordre observable des actions différées** ; liaison et
  portée identiques ne suffisent pas ;
- `defer` contenant journalisation, fermeture conditionnelle, plusieurs appels
  ou tout autre effet ne doit jamais être supprimé sous prétexte qu'il contient
  aussi un `delete` ;
- les diagnostics doivent distinguer redondance sûre, double obligation,
  emprunt vivant et usage après transfert ;
- un outil éventuel doit proposer un diff et préserver commentaires, ordre des
  effets et code de retour. Aucun outil n'est implémenté par cette RFC.

Contre-exemple de migration, exécuté sur `efca644` avec un destructeur qui
imprime `1` : les trois déclarations successives `val r = new Resource()`,
`defer println(2)`, `defer delete r` produisent `1` puis `2` en sortie de portée.
Les remplacer par `using val r = new Resource()` suivi de `defer println(2)`
produit `2` puis `1`. La ressource est bien détruite une fois dans les deux
cas, mais cette migration **doit être refusée** : l'ordre des effets change.
Garder simplement les deux nettoyages ne résout pas le problème : `using val`
suivi de `defer delete` sur la même liaison est rejeté par le diagnostic
« already scheduled », et non fusionné silencieusement.

Les dépendances compilées dans un autre mode gardent leur sémantique interne.
Un changement futur de défaut exigerait une frontière de version explicite,
une stratégie de cache, un audit source et comportemental, et une décision RFC
séparée. Il ne peut pas découler silencieusement du prototype.

## Risques reportés avant généralisation

| Sujet | Pourquoi le premier prototype l'exclut |
|---|---|
| `var` et réaffectation | il faut définir destruction de l'ancienne valeur, échec du nouvel initialiseur, auto-affectation et emprunts actifs |
| propriété/déplacement partiel | il faut des bits d'initialisation par champ et un ordre sûr pour tous les préfixes |
| paramètres possédants | il faut décider si le contrat du callee ou de l'appelant porte le cleanup ; aucun paramètre existant ne doit migrer implicitement |
| champs | la durée de vie est celle de l'agrégat, pas celle d'une liaison locale ; interaction avec constructeurs et moves partiels |
| captures de closures | environnement emprunté ou possédé, capacités `Fn`/`FnMut`/`FnOnce`, escape et cleanup de l'environnement |
| FFI | ownership des handles, callbacks conservées, sorties étrangères et effets inconnus ; le contrôle bas niveau explicite reste nécessaire |
| imports/dépendances | modes mixtes, cache, reproductibilité et cohérence driver/LSP |
| globals | finalisation existante distincte ; risque de double enregistrement |

Ces exclusions ne préjugent pas de leur solution. En particulier, aucune
référence comptée, détection de cycle, copie implicite ou destruction d'un
emprunt n'est réservée pour plus tard.

## Mesures exigées avant une décision de généralisation

Aucune affirmation de performance n'est établie ici. Le prototype, s'il est
approuvé, devra publier la méthode et les valeurs brutes pour :

- allocations et libérations sur un corpus déterminé ;
- nombre, ordre et unicité des entrées de destructeurs ;
- taille/nombre des blocs et appels de cleanup dans l'IR, séparément des
  observations ASan/UBSan runtime ;
- temps de compilation à froid et avec cache, plus taille des entrées ;
- temps d'analyse et mémoire du driver et du LSP ;
- temps d'exécution seulement sur des scénarios assez stables pour être
  interprétables.

Les exit codes normaux et de panique doivent être comparés au témoin. Aucun
résultat non mesuré ne doit être présenté comme « zéro coût ».

## Matrice des critères de l'issue #376

| Critère | Statut dans cette RFC | Suite |
|---|---|---|
| table initialisé/déplacé/détruit | **VÉRIFIÉ EXISTANT** pour `using val` ; proposition identique documentée | comparaison différentielle du prototype |
| ordre et effets/pureté | **VÉRIFIÉ EXISTANT** pour LIFO, seconde panique et `pure def` | refuser toute divergence du prototype |
| normal, branches, boucles, retour, `?`, panique | **VÉRIFIÉ EXISTANT** par corpus ciblé ; aucune forme implicite implémentée | exécuter les paires témoin/candidat |
| négatifs emprunt et double destruction | **VÉRIFIÉ EXISTANT** dans `language.using_val` | dupliquer comme oracle du mode |
| nettoyage unique instrumenté | traces existantes vérifiées pour les fixtures ; critère futur **NON COCHÉ** | compteurs indépendants du corpus prototype |
| compatibilité, FFI, migration, coût runtime | stratégie proposée ; FFI et coûts **INCONNUS / À MESURER** ; critère futur **NON COCHÉ** | validation avant généralisation |

Cette matrice ne clôt ni n'accepte l'issue.

## Preuves reproductibles sur `efca644`

Sources inspectées :

- `docs/language-guide.md`, section « Gestion manuelle de la mémoire » ;
- `src/frontend/parser.cpp` (`parse_block`, `parse_statement`) ;
- `src/semantic/analyzer.cpp` (defer automatique, transferts, emprunts,
  `JANA0043`/`JANA0044`, globales) ;
- `src/backend/llvm/ir_generator.cpp` (`using.armed`, cleanup conditionnel,
  finaliseurs globaux et panic cleanup) ;
- `docs/design/panic-unwinding.md` et `docs/design/pure-functions.md` ;
- `tests/language/pure_function_test.cpp` pour les effets de destructeurs
  explicites, différés, transitifs et agrégés ;
- `tests/language/using_val_test.cpp` et
  `tests/fixtures/runtime/using_val*.janus` avec leurs sorties attendues ;
- `tests/fixtures/runtime/defer_borrow_lifo.janus`,
  `borrow_panic_cleanup.janus`, `panic_context_cleanup.janus`,
  `interprocedural_panic_cleanup.janus` et
  `destructor_panic_cleanup.janus` ;
- `stdlib/std/fs.janus`, `stdlib/std/system.janus` et
  `examples/idiomatic/collections_idiomatic.janus` pour les catégories de
  ressources.

Configuration isolée, avec Clang/LLVM 19.1.7, puis construction minimale :

```text
/opt/data/local/bin/cmake -S . -B build-376 -G 'Unix Makefiles' \
  -DLLVM_DIR=$(/opt/data/local/bin/llvm-config --cmakedir) \
  -DCMAKE_C_COMPILER=/opt/data/local/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/data/local/bin/clang++ \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
/opt/data/local/bin/cmake --build build-376 \
  --target janusc janus_runtime using_val_test janus -j4
```

Commande de validation ciblée :

```text
/opt/data/local/bin/ctest --test-dir build-376 --output-on-failure \
  -R '^(docs\.using_val_doctests|language\.using_val|runtime\.using_val(_try|_panic|_partial_panic|_delete_panic)?|runtime\.defer_borrow_lifo|runtime\.(borrow_panic_cleanup|destructor_panic_cleanup|panic_context_cleanup|interprocedural_panic_cleanup))$'
```

Résultat final observé : **12/12 tests réussis**, 0 échec, en 3,20 s. Le harnais
runtime active son sanitizer d'indéfini pour ces cas ; ce résultat n'est ni une
preuve ASan, ni une mesure de performance, ni une couverture de tous les
programmes. Aucun nouveau fixture n'est nécessaire pour établir l'existant :
les fixtures conventionnelles couvrent déjà le corpus témoin demandé.

Vérification complémentaire indépendante : construction de la cible
`pure_function_test`, puis `ctest --test-dir build-376 --output-on-failure
-R '^language\.pure_functions$'` : **1/1 réussi**. Ces tests couvrent les effets
des destructions explicites et différées ; le lien avec `using val` repose
aussi sur son lowering en defer automatique et le chemin d’analyse commun,
pas sur un nouveau test runtime spécifique combinant `pure def` et `using val`.

## Relation avec #374 et #378

La première tranche de #374 a été fusionnée mais l'issue #374 reste ouverte,
notamment pour les retours propriétaires et empruntés. La présente RFC ne
change pas cette analyse et ne doit pas faire passer une possession de retour
de l'implicite au cleanup local par accident.

#378 peut continuer à définir la syntaxe et l'ABI des captures, mais une
capture ne devient pas automatiquement éligible au nouveau mode. Toute
implémentation #378 qui dépendrait du nettoyage implicite doit attendre une
décision sur cette RFC ; réciproquement, le prototype #376 exclut les captures.
L'ordre proposé est : revue de cette RFC, décision explicite sur le prototype,
prototype isolé si approuvé, mesures, puis seulement décision sur une syntaxe
ou une généralisation. Cette RFC n'est pas réputée acceptée par sa publication.
