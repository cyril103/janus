# RFC : évaluer les effets statiques au-delà de `pure def`

Statut : évaluation pour l’issue #316 ; recommandation **différer**.

Le suivi d’effets apporte une information utile aux callbacks qui peuvent faire
certaines I/O mais ne doivent pas consulter l’horloge ou modifier un état partagé.
Les cas actuels examinés ne justifient cependant pas encore l’ajout transversal
au langage d’un polymorphisme de lignes. Conserver `pure def`, `Result` et des
capacités explicites est la décision proposée avant 1.0. Cette RFC et son
prototype n’introduisent aucune syntaxe, ABI ou API stdlib stable. Une décision
explicite de compatibilité 1.x sera nécessaire avant toute intégration.

## Cas réels et expériences

Les sources liées ci-dessous sont la base de l’évaluation. Les notations
`E(f)`, `forall e` et `!{…}` sont des métadonnées du modèle, **pas du Janus**.
Le prototype représente les appels par un graphe fourni explicitement : il ne
prétend pas extraire tous les effets des corps Janus.

### 1. Chaîne de callbacks génériques depuis une fonction pure

Le [test de pureté](../../tests/language/pure_function_test.cpp) compose déjà
`apply`, une lambda et `identity[int]`. La généralisation à deux niveaux
s’écrit aujourd’hui, avec un contrat pur répété à chaque frontière :

```janus
pure def apply[T](action : pure Fn (T) => T, value : T) : T {
    return action(move value)
}
pure def relay[T](action : pure Fn (T) => T, value : T) : T {
    return apply[T](move action, move value)
}
pure def identity(value : int) : int { return value }
pure def calculate(value : int) : int {
    return relay[int](identity, value)
}
```

Le système actuel vérifie déjà transitivement cette chaîne. Le modèle ajoute
le schéma `forall T,e. E(relay[T,e]) = E(apply[T,e]) = e`, où `e` est l’effet
de la callback. L’instanciation avec `identity[int]` donne `{}`, avec une
callback de lecture de fichier `{ffi,io,panic}`, avec un calcul de durée
`{panic}`. Les trois substitutions sont exécutées par le prototype ; seul le
premier et le troisième effet sont compatibles avec le noyau opérationnel pur.
Les noms du graphe représentent les contrats de callbacks, pas des fonctions
Janus interchangeables malgré leurs signatures différentes.

Gain réel : réutiliser une même chaîne générique pour des callbacks autorisant
uniquement un sous-ensemble d’effets. Pour le seul cas pur, aucun gain de sûreté
sur l’existant. Le modèle spécialise par identité de callback et type concret ;
il démontre la substitution de `e`, pas une unification générale de lignes.

### 2. Frontière I/O et erreur récupérable

[`std.fs.readFile`](../../stdlib/std/fs.janus) renvoie
`Result[FileData, SystemError]`. Aujourd’hui, le résultat expose les erreurs
récupérables ; l’absence de `pure` empêche l’appel depuis une fonction pure.
Le corps appelle une FFI et peut paniquer pendant la préparation des buffers.
Le modèle `fs.readFile -> fs.nativeRead` produit donc `{ffi,io,panic}`.

Une expérience de signature est la paire de métadonnées suivante :

```text
readFile : (Path) -> Result[FileData, SystemError]
E(readFile) = {io, panic, ffi}
```

Gain : différencier cette lecture d’un parseur en mémoire renvoyant le même
`Result`, ou d’une fonction consultant en plus l’horloge. `Result` ne rend
jamais une I/O pure, et retirer `io` ne supprime pas le besoin de traiter
`SystemError`. L’effet est une borne supérieure, sans promesse de succès ni
assertion que toutes les branches font réellement une I/O.

### 3. Panique sans autre effet

[`Instant.durationSince`](../../stdlib/std/time.janus) est déjà
`pure borrow def` et panique lorsque l’instant antérieur est postérieur au
receveur. Le modèle donne exactement `{panic}` ; une durée valide ne panique
pas, mais l’analyse ne cherche pas à prouver cette précondition.

Gain : une bibliothèque pourrait exiger `E(callback) = {}` et refuser cette
callback, là où `pure Fn` l’accepte actuellement. Ce serait un contrat
supplémentaire d’absence de panique explicite, pas une redéfinition de `pure`
ni une preuve de totalité. Convertir une précondition en `Result` reste une
alternative plus locale lorsque l’appelant doit récupérer l’échec.

### 4. Capacité explicite en argument

[`Random`](../../stdlib/std/random.janus) est une capacité concrète existante :
l’appelant fournit un générateur initialisé avec une graine, et `nextUSize`
avance son état. Cela évite l’état automatique partagé de `randomUSize` et
permet des scénarios reproductibles. Aujourd’hui, cette méthode reste impure.
Le modèle lui attribue `{state,ffi}`, et attribue `{random,ffi}` à la création
avec graine système. Une suite pseudo-aléatoire à graine donnée est déterministe ;
`random` désigne ici l’entropie externe, pas chaque tirage du générateur.

Pour la lecture de fichiers, une alternative nominale proposée, non ajoutée à
la stdlib, serait ce schéma de trait et de fonction générique :

```text
trait Reader : read(Path) -> Result[FileData, SystemError]
load[R <: Reader](borrow reader:R, path:Path) = reader.read(path)
```

Une implémentation mémoire peut servir aux tests ; une implémentation disque
porte le résumé `{io,panic,ffi}`. Le nœud `capability.read` du prototype transmet
ce résumé. La capacité limite les opérations accessibles via cet argument,
mais n’interdit pas à son implémentation de consulter une globale ou l’horloge.
Un contrat d’effets ajouterait cette borne transitive. Le coût nominal actuel
est un objet/trait explicite et sa transmission ; il n’exige ni nouvelle
inférence ni nouveau format public. Aucun trait `Reader` permanent n’est créé.

### 5. FFI mathématique pure et FFI avec état

[`std.math.fabs`](../../stdlib/std/math.janus) déclare déjà une FFI pure.
Le contrat actuel suffit pour l’appeler depuis `pure def`. Le nœud `math.fabs`
représente la promesse native déterministe `{}`. À l’inverse, la chaîne de
`Random.nextUSize` modifie le receveur et appelle des fonctions natives ; le
modèle conservateur lui attribue `{state,ffi}`.

Gain : distinguer une FFI contrôlée avec état d’une opération I/O et expliquer
la frontière native dans le diagnostic. Aucun système statique ne vérifie le
corps natif ici. Les résumés sont des assertions de confiance à auditer, y
compris `pure extern`. Le marqueur `ffi` indique une frontière non certifiée
pure, pas toute instruction d’appel native. Une FFI inconnue reçoit la borne
maximale de tous les atomes, jamais seulement `{ffi}` ni `{}` ; le prototype
refuse une cible sans résumé plutôt que de lui inventer un effet vide.

## Sémantique candidate et décisions normatives d’évaluation

Le modèle minimal utilise l’ensemble fermé ordonné canoniquement
`{clock,ffi,io,panic,random,state}`. Composition : union ; compatibilité d’une
callback : inclusion de sa borne dans la borne autorisée. Un effet déclaré
est une borne supérieure contrôlée sur toutes les sorties, y compris les
cleanups normaux, anticipés et de panique. Les effets des destructeurs sont
des arêtes ordinaires du graphe (`scope.exit -> cleanup -> fs.nativeRead`).

| Question | Choix évalué |
|---|---|
| Ensemble fermé, lignes ouvertes ou capacités nominales ? | Ensemble fermé pour les mesures ; variable `e` substituée aux appels génériques. Les lignes ouvertes avec extension/unification et les capacités nominales d’effets sont différées. |
| Relation avec `pure` | Contrat existant distinct, avec projection opérationnelle `E ⊆ {panic}` ; les règles de visibilité, d’ownership et de confiance de `pure` restent nécessaires. `pure` n’est pas synonyme d’ensemble vide. |
| Panique | Effet explicite `panic`, toujours accepté par `pure`. Inclure à terme les traps déterministes des primitives, pas seulement les appels textuels à `panic`. |
| Allocation | Allocation fraîche et mutation locale non suivies. Adresse observable ou accès à un état préexistant sortent du pur. Épuisement mémoire fatal et défaillances de machine restent hors garantie d’absence de panique. |
| État | `state` couvre lecture de globale mutable et mutation visible du receveur/argument/global ; une mutation strictement locale n’en produit pas. La classification exige l’analyse de visibilité existante. |
| I/O, temps, hasard | Atomes séparés ; pas d’implication `clock => io`. Les primitives annoncent tous leurs effets. `random` suit l’entropie externe ; un générateur déterministe mutable produit `state`. |
| Erreur typée | `Result[T,E]`, `Validated` et le protocole `?` restent des valeurs/contrôles de flux ; aucun atome `error`. Les cleanups de `?` contribuent normalement. |
| Callback générique | `forall e` sur une borne de callback, substitution puis union à chaque appel ; aucune recherche implicite de capacité. Une callback inconnue est conservatrice. |
| Absence de panique | Ne garantit ni terminaison, ni absence d’épuisement mémoire, ni sécurité d’une FFI mensongère. Pas d’optimisation automatique. |

Le résumé des fonctions non annotées devrait rester « inconnu », assimilé à
tous les atomes, à une frontière publique. Inférer silencieusement leur contrat
public depuis leur corps rendrait chaque modification d’implémentation
potentiellement incompatible. Une nouvelle version du vocabulaire d’effets
exigerait une nouvelle version de schéma ; un ancien lecteur ne peut ignorer
un atome inconnu.

## Matrice : ownership et effets opérationnels

Les deux analyses restent distinctes. Le système d’effets ne permet jamais un
emprunt invalide, une double consommation ou une fuite de valeur `scoped`.
La distinction n’efface pas les restrictions de `pure` déjà acceptées.

| Forme | Obligation d’ownership | Effet opérationnel possible | Contrat `pure` actuel |
|---|---|---|---|
| `borrow` partagé | durée de vie et absence de transfert | `{}` pour lire la valeur ; I/O possible dans une méthode | annotation pure nécessaire |
| `borrow var` | accès mutable exclusif | `state` lors d’une mutation visible | paramètre refusé par le contrat |
| `consume` méthode | receveur consommé une fois | dépend du corps et du destructeur | incompatible avec `pure` |
| `scoped` callback | ne s’échappe pas | quelconque, y compris `io` | type de callback pur requis pour l’appel pur |
| `Fn` | environnement réutilisable sans mutation | peut appeler horloge, I/O ou globale mutable | capacité seule insuffisante |
| `FnMut` | environnement mutable | `state` si modification visible ; autres effets indépendants | ne confère aucune pureté |
| `FnOnce` | appel consommant, au plus une fois | union du corps et du cleanup | aucune dispense des restrictions de pureté |
| `new`, mutation locale | propriété fraîche, destruction correcte | `{}` si cleanup pur, sinon effets du cleanup | permis selon le contrat existant |

En particulier, passer d’une callback `FnOnce` à `Fn` parce que son résumé est
vide serait incorrect. De même, `tailrec` doit encore vérifier la position
terminale et les cleanups : un résumé vide ne rend pas un appel terminal.
`const def` garde son évaluateur, ses budgets et ses restrictions d’appels ;
il peut fournir un résumé au monde runtime sans accepter les constructions
de `pure def` à la compilation.

## Alternatives comparées

| Alternative | Couverture des cas | Limite / coût |
|---|---|---|
| Statu quo : `pure def` et documentation | chaîne pure, FFI mathématique, frontière impure claire | pas de borne intermédiaire ni contrat sans panique |
| Capacités/traits explicites | générateur déterministe et lecteur injecté ; tests et autorité explicites | ne borne pas les effets cachés de l’implémentation ; arguments à transmettre |
| `Result`, `Validated` | récupération, court-circuit ou accumulation de validations | ne décrit ni I/O ni nondéterminisme ; complément du pur, pas remplacement universel |
| Modules ou annotations nominales sans inférence | sépare les frontières et publie une intention | documentation seule sans vérification ; audit manuel des appels transitifs |
| Ensemble fermé et bornes explicites | composition et callbacks à effets limités | modifie signatures, compatibilité et outils ; vocabulaire à maintenir |
| Lignes ouvertes et polymorphisme complet | bibliothèques abstraites sur des effets extensibles | unification, généralisation, sous-typage et diagnostics beaucoup plus complexes |

Pour les usages réels étudiés, la combinaison des trois premières solutions
couvre le besoin courant. Le bénéfice spécifique restant est la borne
transitive intermédiaire d’une callback et la distinction panique/pureté.
Aucun projet aval fourni ici n’exige actuellement ces deux contrats à une
échelle qui justifie leur coût de migration.

## Prototype, corpus et mesures reproductibles

Le [script isolé](../../scripts/prototype_static_effects.py) et ses
[tests](../../tests/documentation/test_static_effects_prototype.py) s’exécutent
sans compilateur ni dépendance externe :

```bash
python3 scripts/prototype_static_effects.py --repeats 21
python3 -m unittest discover -s tests/documentation -p test_static_effects_prototype.py -v
```

Le [relevé versionné](static-effects-measurements.json) contient les paramètres,
la plateforme et les résultats bruts (Python 3.14.4, Linux x86-64 WSL2).
Il s’agit de 21 nœuds / 15 arêtes modélisés manuellement : mathématiques,
horloge, fichiers, capacité/générateur, callbacks génériques, cleanups et
récursion pure ou impure. Ce corpus représente ces formes d’API ; ce n’est ni
une extraction complète de la stdlib ni un benchmark du compilateur Janus.
Un second corpus synthétique réplique ces formes 100 fois sans arête entre
copies pour examiner l’évolution du coût. Il ne simule pas un grand SCC dense.

| Mesure | Cas représentatifs | 100 copies synthétiques |
|---|---:|---:|
| Nœuds / arêtes | 21 / 15 | 2 100 / 1 500 |
| Visites de worklist | 24 | 2 400 |
| Résolution médiane, 21 exécutions | 0,0249 ms | 3,0923 ms |
| JSON API total / base sans effets | 1 481 / 641 octets | 154 091 / 70 091 octets |
| Supplément de métadonnées d’effets | 840 octets | 84 000 octets |
| Empreintes SHA-256 binaires | 672 octets | 67 200 octets |
| Texte d’affichage sérialisé | 663 octets | 72 291 octets |
| Construction API + empreintes + affichages, un passage | 0,1453 ms | 11,133899 ms |

Les temps sont indicatifs, sans seuil CI, et varient selon la machine. Les
empreintes mesurent 32 octets par résumé (la représentation hexadécimale en
mémoire en utilise davantage). Les tailles JSON ne mesurent pas le RSS Python,
le cache objet Janus, le débit IPC du LSP ou la latence du survol intégré.

Le solveur calcule un plus petit point fixe par union monotone et worklist des
appelants. Chaque nœud gagne au plus six atomes ; les cycles sans source restent
vides, les cycles avec horloge convergent vers `{clock,ffi}`. Hors coût de tri,
la résolution est `O(k(V+E))` pour `k` atomes fixes. Le nombre de spécialisations
peut toutefois exploser indépendamment de ce coût. Une recherche en largeur
fournit une chaîne minimale déterministe vers une source, même dans un cycle :

```text
pipeline[int]/relay<fs.readFile>
  -> pipeline[int]/apply<fs.readFile>
  -> fs.readFile -> fs.nativeRead : io
```

Le test de cohérence compare aussi le solveur à un oracle indépendant par
atteignabilité sur vingt graphes générés avec une graine fixe. Les tests
couvrent substitution des callbacks, cycles, cleanup, diagnostic, rejet des
cibles inconnues et stabilité canonique des empreintes.

L’expérience d’invalidation ajoute `io` à `identity[int]` : exactement trois
empreintes publiques changent (identity, apply et relay spécialisés avec cette
callback), sur 21, soit 18 résumés inchangés. Le modèle recalcule tout le graphe
pour cette comparaison : il mesure la portée d’invalidation, **pas** le temps
d’un cache incrémental. Les contrats publics explicitement bornés exigeraient
d’invalider les consommateurs seulement lorsque cette borne publiée change.

L’inventaire lexical de migration lit les quatre modules sources à chaque
exécution : 33 déclarations de fonctions dans fs dont 1 pure, 13 dans time dont
10 pures, 8 dans random dont aucune pure, 98 dans math dont 97 pures. Au total,
152 déclarations, 108 déjà pures et 44 à examiner pour classifier les fonctions
ordinaires ; les 108 pures nécessiteraient aussi un audit pour distinguer
`{}` de `{panic}`. Ce comptage inclut les déclarations privées et FFI, pas les
coûts humains ni les consommateurs aval ; il n’est pas un audit sémantique.
Avec la décision de différer, la migration obligatoire est de zéro déclaration.

## Impacts d’une éventuelle intégration

| Composant | Travail nécessaire et risque |
|---|---|
| Parser et formatage | comparer annotation préfixée, clause finale ou attribut ; éviter les conflits avec corps `{}`, génériques `[]`, `pure Fn` et clauses de contraintes. Aucune orthographe réservée par cette RFC. |
| Identité des fonctions | inclure la borne et les variables quantifiées dans l’identité ; autoriser une callback moins effectful dans une borne plus large, jamais l’inverse ; normaliser les noms liés par position. |
| Inférence | inférer localement une union après résolution des appels ; exiger les bornes aux frontières publiques. Une ligne ouverte demanderait occurs-check, généralisation et règles de variance supplémentaires non mesurées ici. |
| Traits et méthodes | borne du trait conservée par l’implémentation, éventuellement réduite ; appel via trait utilise sa borne. Aucun dispatch ne doit choisir une méthode par son seul effet. |
| Extensions et types associés | appliquer les mêmes règles aux méthodes d’extension ; normaliser projections/types associés avant substitution des résumés, et maintenir les contraintes du trait lors de la monomorphisation. |
| Monomorphisation | clé comprenant symbole, types concrets et arguments d’effets canoniques ; distinguer spécialisation sémantique et partage éventuel du code machine. Le modèle ne mesure pas le volume LLVM. |
| FFI | résumés audités et versionnés ; une annotation ne vérifie ni le code natif ni ses callbacks ; conserver un mode inconnu conservateur. |
| Cache et compatibilité | ajouter schéma, bornes, variables liées et dépendances aux empreintes existantes ; ordres et doublons sans effet. Versionner le cache plutôt que réinterpréter une ancienne entrée. |
| Mangling et ABI | décider explicitement si les effets distinguent des symboles ; ne rien changer pendant l’évaluation. Tester compatibilité N/N+1 avant publication 1.x. |
| API, documentation et LSP | même forme canonique pour index, hover et diagnostics, par exemple `!{io,panic}` comme affichage expérimental ; distinguer borne déclarée, résumé inféré et confiance FFI ; conserver les positions source sur les arêtes. |

Le prototype encode uniquement la **composante effets** des empreintes ; un
cache intégré doit également conserver signature, ownership, types, corps et
identités de dépendances selon les règles existantes. Il ne traite pas les
positions source LSP, le renommage, l’ABI réelle ni le polymorphisme de lignes
ouvertes. Ces coûts restent non mesurés et interdisent de déduire de ces temps
un budget de compilation de production.

## Décision et conditions de réouverture

Différer l’implémentation et conserver les contrats actuels. Le prototype montre
que l’union d’un ensemble fermé est peu coûteuse sur les formes étudiées ; il
ne démontre pas que le coût transversal d’un langage à lignes ouvertes serait
acceptable. Les alternatives simples répondent aux besoins observés.

Réouvrir lorsque toutes ces conditions sont remplies :

1. deux API Janus ou aval maintenues ont des contraintes intermédiaires de
   callbacks impossibles à garantir avec `pure`, `Result` et capacité explicite,
   accompagnées de régressions concrètes que le système empêcherait ;
2. un prototype intégré mesure sur ces projets la résolution, les grandes SCC,
   les spécialisations, le cache chaud/froid, les diagnostics avec positions et
   la latence LSP, en comparaison du compilateur sans effets ;
3. un budget accepté avant expérimentation borne le coût de compilation et de
   mémoire, et un inventaire sémantique chiffre les annotations à migrer ;
4. une décision de compatibilité 1.x fixe syntaxe, représentation publique,
   mangling, FFI et migration sans modifier le sens actuel de `pure` ;
5. les tests vérifient les interactions avec `const`, `?`, `tailrec`, les
   destructeurs et `Fn`/`FnMut`/`FnOnce` sans affaiblir l’ownership.

Si les capacités et les types de résultat continuent de couvrir ces cas,
rejeter l’extension restera une issue valide. Async, exceptions supplémentaires,
HKT et optimisations automatiques fondées sur les effets restent hors périmètre.
