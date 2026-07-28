# Collections hachées

`HashSet` et `HashMap` partagent les invariants de sondage et de croissance
définis par `std.hash_probe`. Leur surface publique reste inchangée en 0.7.4.

## Invariants de table

- la capacité est au minimum 8 ;
- chaque emplacement est vide, occupé ou supprimé (tombstone) ;
- le sondage est linéaire, commence à `hash % capacity` et visite au plus une
  fois chaque emplacement ;
- un emplacement vide termine une recherche, tandis qu'un tombstone ne
  l'interrompt pas ;
- une insertion réutilise le premier tombstone rencontré si aucune clé
  équivalente ne précède le premier emplacement vide ;
- `length` compte uniquement les emplacements occupés et `deletedCount`
  uniquement les tombstones.

`Hashing.hash` et `Hashing.equals` empruntent leurs opérandes. Deux clés égales
doivent conserver le même hash tant qu'elles appartiennent à la table. Les
types structurels peuvent utiliser `DerivedHashing[T]`; la suite
`runtime.hash_table_invariants` exécute le même corpus avec une stratégie
explicite et une stratégie dérivée.

## Croissance et compaction

Avant une nouvelle insertion, la table compare
`length + deletedCount + 1` au seuil
`capacity - floor(capacity / 4)`, soit 75 % de charge pour les capacités
usuelles. Le calcul n'emploie aucune multiplication et ne peut donc pas
déborder à cause du facteur du ratio.

Le sondage précède la décision de redimensionnement. Un doublon de `HashSet`
ou un remplacement de `HashMap` ne crée aucune entrée et ne rehash donc pas la
table. Pour une insertion réellement nouvelle :

- si les entrées vivantes atteignent le seuil, la capacité double ;
- si seules les tombstones font franchir le seuil, un rehash à capacité
  constante les élimine.

Cette distinction évite de conserver une capacité artificiellement doublée
après une série de suppressions.

## Complexité

Avec une stratégie de hachage distribuée, `contains`, `getOption`, `add`,
`put` et `remove` sont O(1) amorti. Une collision pathologique peut faire
visiter O(capacity) emplacements. Un redimensionnement coûte O(capacity), mais
la croissance géométrique conserve un coût amorti O(1) par insertion.

Les parcours observants et consommateurs inspectent chaque emplacement au plus
une fois, soit O(capacity). Leur ordre dépend de la disposition interne de la
table et ne constitue pas un ordre d'insertion.

Le [benchmark des collections 0.7.4](../benchmarks/hash-collections-0.7.4.md)
mesure notamment les doublons et remplacements au seuil de charge.

## Propriété et nettoyage

Une insertion prend possession de ses arguments. Un doublon de set détruit la
valeur entrante. Un remplacement de map détruit l'ancienne clé équivalente et
transfère l'ancienne valeur dans l'`Option` retournée. Une suppression de map
détruit la clé stockée et transfère sa valeur ; une suppression de set détruit
la valeur stockée.

Le rehash déplace chaque clé et valeur occupée vers le nouveau stockage, puis
détruit l'ancien tableau. `clear`, le destructeur et l'abandon d'un itérateur
consommant libèrent exactement une fois tout élément encore possédé. Les
fixtures ASan couvrent insertions, doublons, remplacements, suppressions,
redimensionnements, builders, destruction anticipée et panique.
