# Protocole `Try`

Statut : accepté, première version implémentée.

## Contrat

L'opérateur `?` repose sur le trait `Try`, exporté par `std.option` :

```janus
trait Try {
    type Output
    type Residual
}
```

Une implémentation est, dans cette première version, un enum à deux branches.
Une branche transporte exactement `Output`; l'autre transporte exactement
`Residual`. Lorsque `Residual` vaut `Unit`, la branche résiduelle ne transporte
aucune valeur. Cette représentation rend la branche explicite tout en permettant
au backend de conserver l'abaissement direct et sans allocation de `Option` et
`Result`.

Une variante générique telle que `Result.Error(E)` est donc abaissée comme une
variante sans payload lorsqu'elle est spécialisée avec `E = Unit`. L'expression
passée au constructeur est toujours évaluée exactement une fois afin de préserver
ses effets, mais aucune valeur `Unit` n'est lue, stockée ou copiée. La propagation
construit uniquement le tag résiduel du type de retour. La compatibilité demeure
stricte : aucune conversion entre résidus distincts n'est implicite.

```janus
enum Attempt[T, E] extends Try {
    type Output = T
    type Residual = E
    Continue(T), Stop(E)
}
```

Les noms du type et des variantes n'ont aucune signification pour le langage.
Le compilateur résout `Output` et `Residual`, identifie les variantes par leurs
types de charge utile et vérifie que le type de retour implémente lui aussi
`Try` avec exactement le même `Residual`. Une conversion de résidu via un futur
trait `From` reste hors périmètre.

## Propagation et propriété

La branche de continuation transfère `Output`. La branche résiduelle construit
une seule fois la variante correspondante du type de retour, exécute les
actions `defer` actives, puis retourne. Un enum propriétaire doit toujours être
déplacé explicitement avant `?`, comme avant cette RFC.

Le même contrat s'applique dans une lambda bloc dont le type de retour est
contextuel. Les diagnostics parlent du contrat `Try` absent, de `Output` ou du
`Residual` incompatible.

## Compatibilité

Les anciens enums génériques à la forme de `Option[T]` ou `Result[T, E]`
restent acceptés sans déclaration de trait. Cette compatibilité structurelle
utilise la position des paramètres génériques et la charge utile des variantes,
jamais leurs noms. Les nouveaux types doivent déclarer `extends Try` et leurs
types associés afin de rendre leur intention explicite.

## Évolutions réservées

- une méthode `branch` personnalisable pour les représentations non enum ;
- une reconstruction `fromResidual` statique lorsque le langage disposera de
  méthodes de trait sans receveur ;
- la conversion du résidu via `From` ;
- les implémentations de `Try` pour des classes et des structs.
