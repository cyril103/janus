# RFC : capacités d'appel `Fn`, `FnMut` et `FnOnce`

Statut : contrat validé et implémenté pour [l'issue #312](https://github.com/cyril103/janus/issues/312).
La capacité est obligatoire dans les signatures. Cette surface reste expérimentale.

## Syntaxe et identité

Le langage ajoute un qualificateur contextuel aux types de fonction :

```text
function-type := ["pure"] ("Fn" | "FnMut" | "FnOnce")
                 "(" parameter-types ")" "=>" return-type
```

L'ordre canonique est `pure Fn (borrow T) => U`. Les modes des paramètres et
du retour restent dans la signature ; `scoped` qualifie toujours le paramètre
qui reçoit la callback. Les trois noms ne deviennent pas des mots-clés
réservés en dehors de cette position et des bornes d'appel.

```text
def inspect(borrow action : Fn (int) => int) : int
def repeat(borrow var action : FnMut () => Unit) : Unit
def run(action : FnOnce () => Unit) : Unit
def withView(scoped action : Fn (borrow Document) => Unit) : Unit
```

La forme historique `(A) => B` est rejetée dans les signatures et annotations
de type, avec un diagnostic demandant de choisir `Fn`, `FnMut` ou `FnOnce`.
Il n'existe ni alias implicite ni quatrième capacité « historique ». Les
expressions lambda gardent leur syntaxe, par exemple `value => value + 1` ou
`() => 42`. Une liaison sans annotation, comme `val f = () => 42`, conserve
l'inférence de capacité ; toute signature publiée l'affiche explicitement.

`pure` est indépendant de la capacité. Une `Fn` peut effectuer des I/O ; elle
ne peut pas modifier son environnement. `pure FnMut` n'autorise aucune
mutation interdite par le [contrat de pureté](pure-functions.md). Un corps
qui modifie une capture reste donc refusé dans ce contexte, même si son type
annoncé est `FnMut`.

La signature complète comprend capacité, pureté, types et modes des paramètres,
ainsi que type et mode du retour. Deux capacités différentes ne sont jamais
égales, même lorsqu'une conversion est possible.

## Conversions

À signature et pureté identiques, la conversion d'une valeur fonctionnelle
est autorisée suivant cette matrice :

| Valeur source | Exigence `Fn` | Exigence `FnMut` | Exigence `FnOnce` |
| --- | --- | --- | --- |
| `Fn` | oui | oui | oui |
| `FnMut` | non | oui | oui |
| `FnOnce` | non | non | oui |

Ces conversions restreignent les usages futurs. Une `Fn` convertie en
`FnOnce` est consommée par son appel, même si son code serait réutilisable.
Le transfert d'une liaison propriétaire exige toujours `move` ; convertir
un emprunt ne crée jamais de propriétaire. Les conteneurs restent invariants :
la matrice ne permet pas de convertir `Box[Fn ...]` en `Box[FnMut ...]`.

Ni la pureté, ni les modes de propriété, ni la garantie `scoped` ne sont
effacés. Une conversion vers une callback possédée est refusée si elle
prolongerait la durée d'une capture empruntée.

## Captures et inférence

La capacité minimale est calculée à partir des opérations sur les captures,
y compris leurs projections et les appels transitifs :

| Opération sur l'environnement | Minimum |
| --- | --- |
| Aucune capture, copie d'un scalaire, lecture ou appel partagé | `Fn` |
| Affectation d'une capture, mutation de champ, appel ou réemprunt mutable | `FnMut` |
| `move`, `delete`, retour possédant ou appel consommant une capture | `FnOnce` |

Une opération mixte prend le maximum `Fn < FnMut < FnOnce`, sur tous les
chemins accessibles. Consommer un paramètre reçu à chaque appel ne rend pas
la closure `FnOnce`. Modifier une variable locale créée à chaque appel ne la
rend pas `FnMut`. Une annotation peut affaiblir la capacité, jamais masquer
une opération incompatible. Une callback capturée impose au moins la
capacité nécessaire à son propre appel.

La capture d'un propriétaire non `Copy` ne devient pas un transfert implicite.
Le compilateur étend l'intrinsèque existant `owningCapture[T](owner, lambda)`
aux signatures de lambda de toute arité et à tout type de retour. Cet
intrinsèque reste une opération explicitement consommatrice de `owner` ;
il n'est pas une copie et invalide la liaison extérieure dès la construction.
Les captures supplémentaires non transférées restent des emprunts. Plusieurs
ressources peuvent être regroupées dans un propriétaire structuré explicite.

```text
def factory[T](value : T) : FnOnce () => T {
    return owningCapture[T](value, () => move value)
}
```

Une capture transférée appartient à l'environnement dès sa construction,
même si le corps ne la consomme pas : un lecteur propriétaire peut être `Fn`.
À l'inverse, ajouter `FnOnce` à une closure empruntante ne l'autorise pas à
consommer la ressource empruntée. Les captures implicites non `Copy` doivent
être vérifiées par les régions lexicales ; les anciens avertissements de
capture propriétaire échappante ne suffisent pas à établir ce contrat.

## Accès et consommation à l'appel

| Mode d'accès à la callback | `Fn` | `FnMut` | `FnOnce` |
| --- | --- | --- | --- |
| Propriétaire disponible | partagé | exclusif | consommation |
| `borrow` | partagé | refus | refus |
| `borrow var` | partagé | exclusif | refus |

Comme pour une méthode mutante sur un objet possédé, la liaison propriétaire
peut être `val` : l'exclusivité concerne l'environnement et ne réassigne pas
la liaison. Une mutation ne traverse jamais un emprunt partagé.

L'accès couvre l'évaluation des arguments et l'appel. Un argument ne peut pas
déplacer, détruire ou réemprunter de manière incompatible la même callback.
Un résultat empruntant l'environnement prolonge l'emprunt jusqu'à sa dernière
utilisation. Une `FnOnce` ne peut pas retourner un emprunt sur l'environnement
qu'elle détruit ; un retour empruntant un argument indépendant suit les règles
de provenance existantes.

`f()` sur une `FnOnce` invalide la place propriétaire lors du transfert à
l'appel. Un second appel, un `move`, un `delete` ou toute lecture de cette
place est alors une utilisation après consommation. Une réinitialisation
explicite d'une liaison `var` crée une nouvelle valeur disponible.

À la jonction des branches, une valeur n'est disponible que si elle l'est sur
tous les chemins qui atteignent la jonction. Les chemins terminés par
`return`, `?` ou `panic` n'y contribuent pas. L'appel dans une boucle d'une
callback créée à l'extérieur est rejeté sans preuve d'absence de seconde
itération ; une callback reconstruite à chaque itération est autorisée.
La même analyse s'applique aux champs et aux alias déplacés, sans copie cachée.

Une action `defer` qui appelle une `FnOnce` réserve sa consommation : un appel
antérieur est refusé. Un `defer delete f` est un nettoyage conditionnel : il
reste actif si aucun appel n'a consommé `f`, et est désarmé après transfert.
Une callback jamais appelée est détruite par `delete` ou son nettoyage
enregistré ; la RFC ne généralise pas la destruction automatique aux valeurs
Janus dépourvues d'un tel nettoyage.

## Destruction et ABI

L'[ABI des closures](closure-abi.md) conserve les trois champs historiques et
ajoute un pointeur de destruction :

```text
{ code: ptr, environment: ptr, owns_environment: i1, drop_environment: ptr }
```

Le thunk `drop_environment(environment)` détruit les seules captures possédées
encore vivantes, dans l'ordre inverse de leur initialisation. Il ne libère pas
le stockage ; le cleanup englobant s'en charge selon `owns_environment`.
Les captures empruntées ne sont jamais détruites. Les captures déplacées ou
détruites pendant l'appel sont désarmées avant leur transfert ou leur destructeur en remettant leur
stockage à zéro. Le cleanup ignore les pointeurs nuls et les closures sans code ;
les captures composites appliquent ce contrôle à leurs champs propriétaires.

L'appel consommant suit un protocole unique, y compris après conversion :

1. Évaluer les arguments sous l'accès réservé à la callback. Jusqu'au
   transfert effectif, les nettoyages de l'appelant restent responsables.
2. Transférer la closure dans un temporaire d'appel et désarmer son ancienne
   place et ses nettoyages. Enregistrer le cleanup du temporaire avant
   d'entrer dans le code de la callback.
3. Exécuter le corps en désarmant les captures transférées ou détruites.
4. Nettoyer les captures restantes et le stockage, sur retour normal comme
   sur panique. Le résultat possédant a déjà été détaché de l'environnement.

Le protocole utilise les [cadres de panique existants](panic-unwinding.md).
Chaque action est retirée avant exécution ; la libération du stockage et les
captures restantes restent enregistrées si un destructeur panique. L'allocation
de l'environnement est protégée avant le transfert de la capture.
Le stockage de pile d'une closure `scoped` n'est pas libéré, mais ses captures
possédées sont détruites. Une lambda sans capture conserve un environnement
nul, sans allocation. Aucune capacité ne nécessite de compteur de références.

## Génériques et outils

Une borne structurelle utilise la même signature, par exemple
`F <: FnOnce (consume T) => U`. Elle permet d'appeler `F` dans un corps générique
et conserve modes et pureté pendant la substitution et la monomorphisation.
Ces bornes sont des capacités du compilateur, sans implémentation utilisateur,
sans nouveau trait dynamique et sans effacement de la signature.

L'implémentation propage une représentation commune de la capacité dans
`TypeReference`, `SemanticType`, les contraintes et les signatures LLVM.
Les conversions sont distinctes de l'égalité des types ; les signatures de
traits, fonctions libres, méthodes et aliases de modules suivent ce même
contrat. Le backend reçoit le plan de captures et de cleanup validé
par l'analyseur, au lieu d'en réinférer séparément la propriété.

L'index d'API utilise le format 2 et l'identité du cache inclut la version 2
de l'ABI des closures. Les signatures sérialisent capacité, pureté, modes des
paramètres et du retour, ainsi que `scoped` sur les paramètres concernés.
Les objets compilés avec l'ancienne ABI sont invalidés. Le formateur préserve
les qualificateurs ; hover, complétion, signature help, semantic tokens et
TextMate affichent la même syntaxe canonique. Les nouveaux diagnostics ont des
codes dédiés et indiquent la capture ou l'appel à l'origine de l'incompatibilité.

## Migration, validation et livraison

La surface reste expérimentale. Les gates concernées
sont `semantic-core`, `surface-inventory` et `ecosystem-validation`. L'inventaire
de stabilité et les fixtures N/N+1 accompagnent le comportement livré.

La migration impose une capacité explicite à chaque ancien type `(A) => B`
dans la stdlib, les exemples, la documentation et les fixtures. Elle ne peut
pas se limiter à préfixer toutes les signatures par `FnMut` : chaque API doit
annoncer le nombre d'appels et l'accès dont son implémentation a besoin.
Les API qui ne font qu'observer une callback reçoivent `borrow ... : Fn ...` ;
les itérateurs paresseux possèdent une `FnMut`, l'appellent avec exclusivité et
la détruisent à leur fermeture. Leurs closures de nettoyage historiques doivent
être auditées : un corps qui détruit son propriétaire devient `FnOnce` et ne
doit pas conserver en plus l'ancien nettoyage manuel du même propriétaire.
Les signatures publiques modifiées nécessitent une migration documentée.

| Matrice de validation | Cas concernés |
| --- | --- |
| Parser et types | Trois capacités obligatoires, rejet des anciens types non qualifiés, lambdas inférées conservées, neuf conversions, refus de perte de `pure`, modes et `scoped`, conteneurs invariants |
| Inférence | Aucune capture, lecture, mutation, consommation ; paramètres et locaux distincts des captures ; callbacks imbriquées |
| Ownership | Zéro/un/deux appels, branches, boucles, réinitialisation, champs, alias déplacé, conflit dans un argument |
| Emprunts | Appels partagés/exclusifs, résultat emprunté, échappement `scoped` refusé pour les trois capacités |
| Génériques et stdlib | `FnOnce` retournant une capture non `Copy`, bornes avec signature, méthodes, aliases, itérateur conservant `FnMut` |
| Runtime, IR et sanitizers | Zéro appel avec cleanup, appel normal, `return`, `?`, panic dans corps/argument/destructeur, construction partielle ; une destruction par capture et une libération par environnement possédé |
| Outils et persistance | Formatage idempotent, LSP complet, TextMate, index N/N+1, cache invalidé par chaque changement d'effet et d'ABI |

La livraison doit maintenir ensemble analyse sémantique et cleanup LLVM :
aucune étape intermédiaire ne doit accepter une consommation de capture que
le backend ne sait pas nettoyer. Les tests existants de fonctions de première
classe, emprunts, pureté, itérateurs et paniques restent obligatoires en plus
des nouvelles preuves. Les tests `language.call_capabilities` vérifient les règles et la validité de
l'IR LLVM ; les fixtures `runtime.call_capabilities*` vérifient les nettoyages
sous ASan et UBSan. Currying, application partielle, HKT, dispatch dynamique et concurrence
restent hors périmètre.
