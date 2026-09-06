# Dérivations structurelles sûres

Statut : implémenté dans Janus 0.6.3.

## Objectifs et limites

Une dérivation réduit le code répétitif nécessaire aux capacités structurelles
`Copy`, `Equality`, `Hashing` et `Debug`. Elle doit rester :

- explicite dans la déclaration du type ;
- fermée à ces quatre capacités, sans mécanisme général de macro ;
- déterministe dans l'ordre des champs et des variantes ;
- compatible avec le modèle de propriété, sans copie ou transfert caché.

Aucune capacité n'est ajoutée parce qu'un type « ressemble » à un type
éligible. L'absence de clause `derives` signifie qu'aucune de ces capacités
n'est demandée.

## Syntaxe

La clause suit les paramètres génériques et, pour une classe ou un struct, une
éventuelle clause `extends`. Elle précède le corps :

```janus
struct Point(val x : int, val y : int)
derives Copy, Equality, Hashing, Debug {}

enum Status[T] derives Equality, Debug {
    Ready(T),
    Failed
}

class Report(val message : string)
derives Equality, Hashing, Debug {}
```

Les noms sont sensibles à la casse et limités à `Copy`, `Equality`, `Hashing`
et `Debug`. Un nom inconnu ou répété est une erreur. L'ordre écrit n'affecte
pas le résultat, mais le formatter le conserve. Les traits ne portent pas de
clause `derives` : ils décrivent un contrat, pas une représentation stockée.

`Hashing` exige que `Equality` soit également demandée dans la même clause.
Cette dépendance reste explicite : demander `Hashing` n'ajoute jamais
implicitement `Equality`.

## Éléments structurels pris en compte

Les éléments sont visités dans un ordre canonique :

- pour un struct, les champs du constructeur puis les champs du corps, dans
  l'ordre source ;
- pour un enum, les variantes dans l'ordre source puis leurs payloads de
  gauche à droite ; le discriminant participe à `Equality`, `Hashing` et
  `Debug` ;
- pour une classe, les champs stockés du constructeur puis ceux du corps.
  Les paramètres de constructeur qui ne sont pas des champs sont exclus.

La visibilité d'un champ ne modifie pas ce parcours. Le code synthétisé est
dans la portée du type et peut donc observer ses champs privés et internes,
mais il ne les expose jamais. Une capacité synthétisée possède la même
visibilité que son type ; aucun symbole public n'est produit pour un type
privé.

Les méthodes, initialiseurs, paramètres non stockés et destructeurs ne
participent pas à la valeur structurelle. Un champ `var` est comparé, haché ou
affiché selon sa valeur au moment de l'observation.

## Éligibilité par capacité

### `Copy`

`Copy` est disponible uniquement pour les structs et enums. Chaque champ ou
payload doit lui-même satisfaire `Copy`, récursivement. Une classe, une
closure propriétaire, un pointeur propriétaire ou un agrégat qui en contient
rend la dérivation impossible.

Une classe ne peut jamais dériver `Copy`, même lorsque ses champs sont
copiables : copier son identité dupliquerait une référence propriétaire.

### `Equality`

Tous les champs ou payloads doivent prendre en charge `Equality`. L'opération
synthétisée observe ses deux opérandes et ne les déplace ni ne les détruit.
Pour un enum, deux valeurs de variantes différentes sont inégales sans
observer leurs payloads.

Une classe qui demande `Equality` choisit explicitement l'égalité structurelle
plutôt que l'identité. Un cycle de classes structurelles est refusé afin
d'éviter une récursion non bornée.

### `Hashing`

`Hashing` requiert `Equality` et exactement le même ensemble de champs. Deux
valeurs égales doivent produire le même hash. Le mélange commence par
l'identité nominale du type ; un enum mélange ensuite son discriminant avant
les payloads actifs. Aucun payload inactif n'est lu.

L'opération est une observation bornée. Elle ne déplace pas une clé
propriétaire et n'introduit pas d'allocation obligatoire.

### `Debug`

`Debug` exige que chaque élément stocké prenne en charge `Debug`. La sortie est
déterministe et distincte du format destiné à l'utilisateur :

```text
Point { x: 1, y: 2 }
Status.Ready(42)
```

Les chaînes, qu'elles soient passées directement à `debug` ou imbriquées dans
un agrégat dérivé, sont entourées de guillemets doubles. `"`, `\`, LF, CR,
tabulation et NUL sont écrits respectivement `\"`, `\\`, `\n`, `\r`, `\t` et
`\0`. Les autres contrôles C0 et DEL utilisent `\xNN`, avec deux chiffres
hexadécimaux majuscules. Les octets UTF-8 imprimables sont conservés tels
quels. Ainsi, `debug("A\"B\nC:\\scores")` produit exactement une ligne :

```text
"A\"B\nC:\\scores"
```

Ce format est destiné aux diagnostics et peut évoluer ; ce n'est pas un
format de sérialisation. Une chaîne Janus valide contient de l'UTF-8 valide,
mais `Debug` n'ajoute aucune normalisation Unicode et ne rééchappe pas les
caractères Unicode hors C0/DEL. `print` et `println` continuent d'écrire le
contenu brut des chaînes.

Les noms privés des champs peuvent apparaître dans cette représentation de
diagnostic, mais leur valeur n'est pas rendue accessible au programme.
Comme pour `Equality`, les cycles structurels de classes sont refusés.

## Génériques

La demande appartient au type générique, mais son éligibilité est
conditionnelle. Un paramètre générique utilisé dans un champ doit prendre en
charge la capacité lors de chaque spécialisation :

```janus
struct Pair[T](val left : T, val right : T)
derives Equality, Debug {}
```

`Pair[int]` dispose de ces capacités si `int` les fournit. Une spécialisation
avec un type incompatible est valide tant que la capacité dérivée n'est pas
requise ; son utilisation produit un diagnostic sur `left` ou `right`. Pour
`Copy`, les contraintes explicites existantes `T <: Copy` permettent de
rejeter plus tôt une déclaration manifestement impossible.

Une dérivation ne satisfait pas un trait arbitraire portant le même nom dans
un autre module. Les quatre noms de la clause désignent des capacités
intrinsèques. Une implémentation manuelle et une dérivation de la même
capacité sur le même type sont incompatibles et produisent un diagnostic.

## Diagnostics

Le diagnostic principal nomme le type, la capacité et le premier chemin
stocké fautif. Les chemins imbriqués sont conservés :

```text
cannot derive Copy for 'Packet': field 'payload.resource'
has owning type 'Resource'
```

Pour un enum, le chemin inclut la variante et la position du payload :

```text
cannot derive Hashing for 'Event': case 'Data' payload 2
does not support Hashing
```

Les erreurs sont rapportées à l'emplacement de la capacité demandée, avec une
note à l'emplacement du champ ou payload fautif. Si plusieurs éléments sont
invalides, le compilateur les rapporte dans l'ordre structurel canonique. Les
erreurs de déclaration (`Hashing` sans `Equality`, doublon, capacité inconnue
ou `Copy` sur une classe) précèdent l'analyse champ par champ.

## Propriété et génération

`Equality`, `Hashing` et `Debug` empruntent leurs opérandes pendant la durée de
l'appel. Leurs implémentations synthétisées ne peuvent pas :

- employer `move` ou `delete` sur un opérande ou un champ ;
- retourner une référence empruntée ;
- conserver un opérande dans une closure ou un objet ;
- visiter le payload inactif d'un enum.

`Copy` n'est généré qu'après preuve récursive qu'aucune ressource ne serait
dupliquée. Les fonctions synthétisées suivent les règles ordinaires de
visibilité, de monomorphisation et de résolution de traits. Leur détail ABI et
leurs noms internes ne font pas partie de la surface source.

R063-1 réserve et transporte la demande dans l'AST ; R063-2 valide cette
métadonnée et génère les opérations correspondantes.

## Surface d'utilisation

`Equality` fournit les opérateurs `==` et `!=`. `Hashing` fournit la stratégie
standard `DerivedHashing[T]` du module `std.hashing`, utilisable directement
comme paramètre de `HashSet` ou `HashMap` :

```janus
val hashing : DerivedHashing[Point] = new DerivedHashing()
val points : HashSet[Point, DerivedHashing[Point]] =
new HashSet(8, hashing)
```

L'intrinsèque `debug(value)` écrit une ligne déterministe sur la sortie
standard. Il s'agit volontairement d'une représentation de diagnostic :
`print` et `println` n'acceptent pas automatiquement les agrégats dérivés.
Les opérations de hachage internes empruntent leur valeur ; leur nom et leur
ABI ne font pas partie de la surface stable.

## Contrat d'outillage

`derives` est un mot-clé réservé. Le parser conserve chaque demande sous une
forme typée dans l'AST et rejette immédiatement les noms inconnus et doublons.
Le formatter conserve la clause et reste idempotent. Le serveur LSP accepte
ces déclarations sans diagnostic syntaxique et propose `derives` dans la
complétion des mots-clés. Ces garanties empêchent la syntaxe de diverger
pendant l'implémentation de R063-2.
