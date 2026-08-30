# Types associés aux traits

Statut : accepté, première version implémentée.

## Motivation

Un type associé exprime une relation fonctionnelle entre une implémentation de
trait et un type produit par cette implémentation. Il évite de transmettre ce
type comme paramètre générique à chaque niveau d'une API.

```janus
trait Producer {
    type Item
    def next() : Item
}

class Tokens() extends Producer {
    type Item = Token
    def next() : Token { ... }
}
```

## Syntaxe et portée

Une déclaration `type Name` est autorisée dans un trait. Le nom est visible
sans qualification dans toutes les signatures de méthodes du trait. Une classe
qui implémente le trait fournit exactement une définition `type Name = Value`
dans son corps. Une définition ne porte pas de modificateur de visibilité : sa
visibilité est celle du trait et de l'implémentation.

Une fonction générique projette le type avec `T.Name`, à condition que `T` soit
contraint par un trait qui déclare `Name` :

```janus
def produce[P <: Producer](producer : P) : P.Item {
    return producer.next()
}
```

Cette première version n'autorise ni paramètre générique ni borne sur la
déclaration associée. Les égalités de projections dans une clause `where` sont
réservées à une évolution ultérieure.

## Résolution et cohérence

Une projection reste symbolique pendant l'analyse d'une fonction générique.
Lorsque son paramètre reçoit une classe concrète, elle est normalisée avec la
définition de cette classe. La comparaison des signatures du trait et de la
classe utilise le même type normalisé.

Une classe doit définir tous les types associés de chaque trait implémenté. Une
définition qui ne correspond à aucun de ces traits est rejetée, comme le sont
les doublons. Deux implémentations du même trait par une classe restent
interdites par la règle de cohérence existante.

La normalisation suit les références entre définitions associées et maintient
un ensemble des projections en cours. Revoir une projection active produit un
diagnostic de cycle immédiatement ; la recherche est donc bornée par le nombre
de définitions de la classe.

## Compilation et outils

Les projections normalisées alimentent la monomorphisation et le backend LLVM ;
elles ne changent pas le dispatch statique. Les déclarations et définitions font
partie de l'empreinte d'interface du cache incrémental. L'index d'API et la
documentation les publient comme symboles `associated-type`, et le lexer du LSP
classe `type` comme mot-clé.

Le mangling des méthodes reste fondé sur les arguments génériques concrets. Le
type associé normalisé participe déjà à leur signature LLVM, sans introduire de
nouvelle identité nominale.

## Hors périmètre

- types associés génériques ou contraints ;
- égalités de projections dans `where` ;
- [types de rang supérieur](higher-kinded-types.md) ;
- implémentations chevauchantes ou spécialisations incohérentes ;
- dispatch dynamique.
