# RFC : évaluer un contrat optionnel `total def`

Statut : évaluation de l’[issue #317](https://github.com/cyril103/janus/issues/317).
Décision proposée : **différer l’intégration**, garder deux obligations internes
séparées (absence de panique et terminaison). `total` et `no_panic` ne deviennent
pas des mots-clés. Cette RFC ne change ni `pure def`, ni les fonctions ordinaires,
ni l’ABI, ni les optimisations.

## Trois propriétés distinctes

La **pureté** borne les effets observables. L’**absence de panique** exclut les
sorties anormales du modèle. La **terminaison** exige un nombre fini d’étapes
pour chaque entrée valide, sans promettre de délai raisonnable. Une récursion
pure peut boucler ; une division pure peut paniquer ; une boucle sans panique
peut ne jamais retourner. `Result.Error` et `Option.None` sont des valeurs de
retour normales et ne constituent pas des échecs de totalité.

Le [contrat actuel de pureté](pure-functions.md) accepte `panic`. `tailrec`
contrôle les appels terminaux et leur compatibilité avec le nettoyage ; il ne
prouve aucune décroissance. `const def` possède un évaluateur et des budgets,
mais l’épuisement d’un budget ne constitue pas une preuve de non-terminaison,
ni le succès d’une évaluation une preuve pour toutes les entrées.

## Définition normative candidate, limitée à une première version

Une fonction explicitement déclarée `total def` devrait être pure et, pour
chaque entrée valide de son type, retourner une valeur du type déclaré en un
nombre fini d’étapes, sans panique, trap déterministe ni comportement indéfini.
La validité est celle des types et invariants vérifiés par le langage : une
précondition écrite en commentaire, un `assert` ou une annotation utilisateur
non vérifiée ne réduit jamais le domaine des entrées.

Le modèle abstrait suppose assez de mémoire et de pile, une machine correcte
et l’absence d’arrêt externe. L’épuisement physique mémoire/pile est hors modèle.
En revanche, une panique explicite de bibliothèque lors d’une allocation,
d’un contrôle de capacité ou d’un overflow reste dans le modèle et interdit
la preuve. Il faut distinguer le manque physique de ressources d’une limite
représentable atteinte par une opération. Ce contrat n’est donc pas une garantie
de disponibilité temps réel ou sous quota mémoire.

La première version implique `pure` : cela réutilise le contrôle existant de
l’état partagé, des entrées/sorties et des cleanups. Cette décision simplifie
les obligations ; elle ne prétend pas qu’une fonction avec état ne puisse
mathématiquement terminer. Les fonctions existantes, même reconnues sûres par
l’expérience, ne reçoivent aucun contrat public implicite.

| Opération ou construction | Règle candidate |
|---|---|
| Valeur, emprunt valide, déplacement, lecture d’un champ immutable, comparaison primitive | Admis après typage et ownership ; une surcharge est un appel à vérifier. |
| Retour et branches | Tous les chemins retournent le type déclaré ; `if` vérifié sur ses deux branches, `match` exhaustif sur toutes les variantes. Aucun retour manquant accepté comme preuve. |
| Entiers | Comparaisons et opérations checked produisant `Option`/`Result` admises. Arithmétique brute seulement avec preuve de représentabilité, même si l’opération actuelle wrappe ; c’est une restriction du sous-ensemble, aucun changement de sémantique. |
| Division et reste | Exiger diviseur non nul et exclure `MIN / -1` et `MIN % -1` signés ; à défaut utiliser une primitive checked vérifiée. Le prototype ne prouve pas les divisions brutes, même gardées. |
| Conversions | Identité, élargissement exact vérifié ou conversion checked ; narrowing, flottant vers entier, décalage hors largeur et conversions non modélisées rejetés. |
| Flottants | NaN et infinis sont des valeurs ; aucune promesse de résultat réel fini. Primitives et modes de traps doivent avoir une sémantique auditée. Non modélisés par le prototype initial. |
| Accès collection | Accès vérifié retournant `Option`/`Result` admis si son implémentation et ses cleanups le sont ; indexation brute rejetée dans la première version. Un nom `get` ne certifie rien. |
| `panic`, `unreachable`, assertions paniquantes | Toujours refusés, même dans une branche que l’auteur pense inatteignable ou sous `if false`. Pas d’élimination de branches dépendant des optimisations pour établir le contrat. |
| Allocation | Allocation fraîche compatible avec `pure`, sous le modèle de ressources ci-dessus ; vérifier tailles, constructeurs, compteurs de références et destructeurs. `new` seul n’est pas une preuve. |
| Destruction, `defer`, `?`, retours anticipés | Chaque cleanup est une arête du graphe, avec les mêmes exigences de pureté, absence de panique et terminaison. Ni destructeur inconnu de `T`, ni panic cleanup ignoré. |
| FFI | Refusée, même `pure extern`, sans certification du compilateur auditée et versionnée couvrant tous les arguments et callbacks. Aucune certification utilisateur ; la première expérience n’en inclut aucune. |
| Appels | Un autre contrat `total` doit avoir une preuve valide, y compris les appels transitifs et implicites ; fonction ordinaire, callback ou dispatch inconnu rejeté. |
| Boucles | Borne finie vérifiée et compteur protégé de l’overflow ; première expérience limitée à une borne entière constante non négative. `while` arbitraire rejeté. |
| Récursion | Sous-terme strict d’un enum inductif fini ou variant entier minoré et strictement décroissant. Cycles mutuels rejetés initialement, même si une preuve plus puissante pourrait les accepter. |

Le sous-ensemble peut rejeter un programme effectivement total : la priorité
est de ne pas accepter un cas non prouvé. Il reste toujours possible d’écrire
ce programme comme fonction ordinaire. Une preuve de totalité ne dispense
jamais de satisfaire les règles de typage, ownership, `pure` et `tailrec`.

## Preuves de terminaison retenues

Pour un argument entier immutable `x`, la branche `x > 0` peut rappeler la même
fonction avec `x - 1`, la branche restante retournant directement. Le variant
est `max(x, 0)` dans les naturels. Le test protège aussi la soustraction de
l’underflow ; décrémenter sans garde ou dans la branche négative est refusé.
Une simple annotation « décroissant » fournie par l’utilisateur ne suffit pas.

Pour un enum inductif fini `List = Nil | Cons(value, tail)`, un `match` exhaustif
peut rappeler la même fonction sur le champ `tail` de la branche `Cons`. Le
variant est le nombre de constructeurs restants. Une reconstruction de la
liste, un rappel sur l’argument entier ou un graphe cyclique n’est pas un
sous-terme strict. La liste du prototype est un enum abstrait emprunté ; ce
n’est pas une certification du moteur `PersistentList`, de `Shared`, ou de
leurs opérations et destructeurs natifs.

Formes conceptuelles, **pas une syntaxe Janus livrée** :

```text
total tailrec def countdown(x : int) : int {
    if x > 0 { return countdown(x - 1) }
    return 0
}
total tailrec def walk(borrow xs : List[T]) : bool {
    return match xs { Nil => true, Cons(value, tail) => walk(tail) }
}
```

Les deux obligations restent indépendantes : `walk(xs)` dans la branche
`Cons` peut respecter la position terminale mais échoue à la preuve de
terminaison. Une récursion structurale non terminale nécessiterait en plus
une opération totale pour combiner les retours et reste soumise aux règles
actuelles de récursion. L’ordre des cleanups doit être préservé.

## Prototype exécutable et frontière de confiance

Le [prototype Python](../../scripts/prototype_total_functions.py) analyse une
**IR typée abaissée manuellement**, comme l’expérience sur les
[effets statiques](static-effects.md). Il ne lit pas les corps Janus pour
inférer leurs propriétés et ne constitue pas une preuve sur le compilateur.
Il n’accepte aucun booléen utilisateur « termine » ou « décroît » : les preuves
sont reconnues depuis les formes de branches, d’arguments et de sous-termes.

Chaque fonction modèle possède un argument immutable `x`, une catégorie de
type et un corps expression retournée. Un corps absent, une forme inconnue,
un `match` incomplet et un appel inconnu échouent conservativement. Le typage,
la pureté, la validité des emprunts et le caractère fini de l’enum sont des
prérequis du lowering, à vérifier par le frontend lors d’une intégration.
Les primitives IR `checked_div`, `checked_add`, `checked_cast` et `get_option`
représentent des opérations intrinsèquement vérifiées retournant une erreur
comme valeur ; elles ne sont pas une liste blanche de fonctions stdlib.
Le second opérande représente respectivement le diviseur, l’opérande,
le descripteur de conversion et l’index. Aucun exécuteur de ces valeurs n’est
implémenté : le prototype vérifie uniquement leurs obligations.

Le graphe inclut les appels dans les arguments, toutes les branches et les
cleanups modélisés par `seq`. Kosaraju itératif calcule les composantes fortement
connexes sans épuiser la pile Python sur un long cycle. Seuls les appels à soi
avec décroissance reconnue sont admis dans une composante récursive ; une arête
mutuelle est une source locale de rejet. Une propagation en largeur sur les
arêtes inverses fournit un témoin court par fonction rejetée, par exemple :

```text
apply[int,panic.leaf]:return -> panic.leaf
panic.leaf:return: unproved operation panic

mutual.a:return: recursive edge mutual.a -> mutual.b has no proven decrease
```

Les chemins nomment la fonction et l’emplacement dans l’IR (`return.Cons`,
`return.argument`, `return.seq[1]`), pas une fausse position source Janus.
Un diagnostic contient au plus un passage par fonction ; le test d’un cycle
mutuel de 1 500 fonctions produit un témoin local d’une ligne par fonction.
La construction des composantes et la propagation sont linéaires hors tri ;
la matérialisation des chemins peut coûter `O(V²)` en temps et mémoire sur une
longue chaîne. Une intégration devrait garder des liens parents et limiter
l’affichage, avec accès au chemin complet sur demande. Les arbres IR du
prototype sont parcourus récursivement : la limite Python sur un corps très
profond est une autre limite de l’expérience, distincte des cycles d’appels.

## Corpus, mesures et faux négatifs

Reproduction locale, sans dépendance externe :

```bash
python3 scripts/prototype_total_functions.py --repeats 21
python3 -m unittest discover -s tests/documentation -p test_total_functions_prototype.py -v
```

Le [relevé JSON](total-functions-measurements.json) conserve plateforme, version
Python, médianes de 21 exécutions, diagnostics et SHA-256 des quatre fichiers
sources étudiés. Les tests exigent un nouvel audit du modèle si ces sources
changent. Les temps mesurent uniquement l’analyse Python, pas le parsing,
l’analyse sémantique, LLVM ou le LSP. Aucun seuil de performance CI n’est inféré.

Le corpus complet comprend 35 fonctions et 14 arêtes, dont les cinq
contre-exemples obligatoires : division par zéro, index potentiellement hors
borne, panique transitive, boucle infinie et cycle mutuel non décroissant.
Tous sont rejetés. Les acceptations comprennent branchement, match exhaustif,
appel transitif, récursion structurale, variant entier, boucle bornée et accès
checked. Les tests couvrent aussi mauvaise branche de garde, rappel sans
progrès, cleanup paniquant, FFI, overflow non prouvé et retour manquant.
Un oracle indépendant d’atteignabilité vérifie les composantes sur vingt
graphes pseudo-aléatoires à graine fixe.

L’échantillon stdlib est **manuel et sélectionné**, neuf fonctions dans quatre
modules ; il ne permet pas d’extrapoler un taux à la stdlib entière :

| Source et fonctions | Oracle manuel sous le modèle | Prototype `total` |
|---|---|---|
| `std.option.isSome`, `isNone` | Totales : emprunt, deux variantes, retour booléen | 2 acceptées |
| `std.result.isOk`, `isError` | Totales : emprunt, deux variantes, retour booléen | 2 acceptées |
| `PersistentList.size`, `isEmpty` | Totales : lecture du champ longueur, comparaison | 2 acceptées |
| `std.math.gcd` | Total : reste non signé, diviseur gardé, variant `next` | Rejet : boucle euclidienne non modélisée |
| `std.math.fabs` | Total sous les hypothèses de la primitive native flottante | Rejet : FFI non certifiée |
| `std.math.lcm` | Non total : panique explicite sur overflow | Rejet |

Ainsi 6 des 8 cas jugés totaux sont reconnus ; **2/8 faux négatifs (25 %)**,
aucun faux positif sur les neuf exemples. L’oracle de `fabs` est conditionnel
à la sémantique native documentée, pas un audit exhaustif de chaque plateforme.
Les corps `gcd` et `fabs` sont volontairement opaques dans l’IR et `lcm` est
réduit à son appel et sa panique : le chiffre décrit la couverture du modèle,
non celle d’un analyseur source. Aucun coût humain de migration n’est mesuré.

Quatre spécialisations `apply[int| string, identity| panic.leaf]` vérifient que
la substitution du callback ne confond pas les preuves : les deux variantes
avec `identity` passent, celles avec panique échouent. Deux nœuds de callbacks
complètent ce sous-corpus. Le remplacement du corps d’`identity` par un appel
paniquant invalide ses deux appelants. Ceci ne mesure ni expansion réelle des
types Janus ni coût des destructeurs génériques ; les prédicats empruntés du
corpus stdlib évitent précisément de détruire `T`.

## Alternatives comparées

Le mode `no_panic` du même solveur retire seulement les obligations de
terminaison, en gardant les mêmes limites de primitives et de lowering. Il
sert à isoler ce gain de couverture ; son nom ne désigne pas une syntaxe livrée.

| Alternative | Résultat mesuré / couverture | Limite et décision |
|---|---|---|
| `total def` | 6/9 fonctions stdlib acceptées ; rejette les cinq contre-exemples | Deux preuves de totalité manquées ; différer l’intégration. |
| `no_panic def` seul | Toujours 6/9 sur cet échantillon ; accepte boucle infinie et cycle mutuel du corpus | Les inconnus `gcd`/FFI restent inconnus ; ne promet jamais de retourner. Garder comme obligation interne. |
| Deux contrats séparés | Les modes du prototype séparent effectivement cycles et paniques | Pas d’expérience sur syntaxe, combinaison ou ergonomie ; ne pas ajouter deux annotations publiques prématurément. |
| Lint non bloquant | Peut émettre les mêmes diagnostics et a le même coût d’analyse | Zéro programme bloqué mais zéro garantie imposée ; prochaine expérimentation possible sur IR compilateur. |
| Tests et fuzzing, statu quo | Zéro annotation et zéro coût de cette analyse en compilation | Peuvent trouver division/index invalides ; un timeout ne prouve pas une terminaison universelle. Aucun benchmark de fuzzing effectué ici. |
| `Result` obligatoire aux frontières faillibles | Les primitives checked du corpus sont admises | Une fonction retournant `Result` peut encore boucler ou paniquer dans un cleanup ; complément utile au statu quo. |
| Totalité réservée à `const def` | Pas de prototype intégré mesuré | Le budget const-eval limite une exécution, pas toutes les entrées ; réduit les usages runtime sans résoudre la preuve. |

Les médianes complètes et celles de l’échantillon stdlib pour les deux contrats
figurent dans le JSON, ainsi que le coût du sous-corpus générique. Le statu quo
ajoute exactement zéro passe de totalité ; comparer ce zéro à la médiane Python
ne constitue pas une estimation du surcoût de compilation Janus. La précision
et la migration sur un projet aval restent **non mesurées**.

Relevé local de 21 exécutions (millisecondes, valeurs indicatives) :

| Analyse | Corpus complet, 35 fonctions | Échantillon stdlib, 9 fonctions |
|---|---:|---:|
| `total` | 0,1439 ms ; 16 acceptées | 0,0387 ms ; 6 acceptées |
| `no_panic` | 0,1339 ms ; 20 acceptées | 0,0385 ms ; 6 acceptées |

Le sous-corpus de quatre spécialisations et deux callbacks prend 0,0244 ms
en médiane. Ces différences très petites sont sensibles au bruit de mesure ;
elles ne démontrent pas un avantage de performance en production.

## Impacts d’une éventuelle intégration

| Composant | Décision / travail requis |
|---|---|
| AST, parser, formatter | Un modificateur explicite de déclaration, pureté impliquée, combinaisons avec `tailrec` validées ; aucune syntaxe réservée maintenant. |
| CFG et graphe d’appels | Vérifier tous les retours après typage ; enregistrer appels implicites, lieux source et cleanups ; preuves séparées de l’optimisation du CFG. |
| `pure`, `const`, `tailrec` | Garder leurs contrats actuels. Une preuve `total` ne permet ni évaluation constante automatique, ni transformation terminale invalide, ni suppression d’un appel. |
| Ownership / destructeurs | Vérifier chaque destruction effective après spécialisation ; emprunt de `T` sans destruction est indépendant de son destructeur, possession ne l’est pas. |
| Génériques | Prouver symboliquement pour tous les types satisfaisant les bornes. Callback/trait/destructeur sans contrat vérifié rejeté. Une spécialisation réussie ne certifie pas le template. |
| Traits et dispatch | Une éventuelle borne totale du trait exige une preuve pour toutes ses implémentations ; aucune sélection de surcharge par la preuve. Dispatch inconnu refusé. |
| Monomorphisation | Revalider les primitives, bornes numériques, cleanups et obligations après substitution ; interdire d’exporter un contrat fondé seulement sur les instanciations rencontrées. |
| FFI | Table interne auditée par cible, versionnée ; confiance native distincte d’une preuve sur corps Janus. Pas d’attribut utilisateur permettant de contourner le vérificateur. |
| Cache et API index | Versionner schéma et règles de preuve ; empreinte du corps, types, bornes, dépendances, cible et certifications ; résultat inconnu en cas d’ancienne preuve. |
| Documentation et LSP | Afficher contrat déclaré et état vérifié séparément ; diagnostic à la déclaration puis chaîne source jusqu’à la cause, avec limite d’affichage. |
| Stabilité | Une annotation publique promise est un contrat de compatibilité ; un nouveau vérificateur rejetant une ancienne preuve demande une politique versionnée et des tests N/N+1. |
| ABI et optimisations | Aucun changement pendant l’évaluation. Même une preuve valide n’autorise pas à ignorer allocation/ownership ; optimisation soumise à une proposition distincte. |

## Décision et critères de réouverture

Différer `total def` et ne pas publier `no_panic` pour l’instant. Les preuves
syntaxiques simples sont utiles mais les 25 % de faux négatifs sur ce petit
échantillon choisi, les FFI et les cleanups génériques empêchent d’annoncer un
contrat largement utilisable. Les mesures ne justifient ni rejet définitif du
concept, ni activation d’une optimisation.

Réouvrir avec les éléments suivants :

1. un prototype sur l’IR réelle, produisant des positions source et vérifiant
   pureté, ownership, chemins de retour et cleanups sans lowering manuel ;
2. un corpus stdlib élargi et au moins un projet aval maintenu, avec oracle
   revu, cas non triviaux, faux négatifs expliqués et aucun contre-exemple
   connu accepté ;
3. des budgets fixés avant mesure pour temps, mémoire, grandes composantes,
   spécialisations, cache froid/chaud et LSP, comparés au compilateur courant ;
4. une décision explicite sur les certifications natives, les bornes génériques,
   le modèle d’allocation et la stabilité des preuves entre versions ;
5. une comparaison d’ergonomie du lint, de `no_panic` et du contrat combiné
   sur les API aval, avant de choisir implémenter, scinder ou rejeter.

Aucune optimisation ni promesse ABI ne doit être activée avant cette validation
stdlib **et** aval. Le projet aval absent est une condition de réouverture,
pas une validation prétendument accomplie par cette RFC.
