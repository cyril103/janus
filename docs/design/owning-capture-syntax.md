# RFC de tranche : syntaxe de capture propriétaire

Statut : tranche additive de l'issue #378. Cette RFC ne valide ni ne reprend le
prototype de nettoyage automatique étudié par #376.

## Surface retenue

Une lambda peut transférer exactement une liaison locale propriétaire :

```janus
val read : Fn () => int = [move resource] () => resource.value
```

La grammaire de cette tranche est volontairement étroite :

```text
owning-lambda := "[" "move" identifier "]" lambda
lambda        := identifier "=>" expression
               | "(" parameters? ")" "=>" (expression | block)
```

Le préfixe n'est reconnu comme capture que lorsque `[` est immédiatement suivi
du mot-clé `move`. Les littéraux de tableau et de map, l'indexation postfixe et
les arguments de types génériques conservent donc leur grammaire. Les
parenthèses de la lambda restent obligatoires pour zéro ou plusieurs
paramètres ; `[move owner] value => value` reste permis pour un seul paramètre
nu, exactement comme la lambda historique.

Le nom est résolu dans la portée extérieure au point de construction. Un
paramètre ou une locale du corps portant le même nom ne peut pas servir de
propriétaire : le corps doit effectivement capturer la liaison extérieure.

## Abaissement et sémantique

`[move owner] lambda` est une représentation AST structurée, abaissée vers le
même contrat analysé et le même plan backend que
`owningCapture[OwnerType](owner, lambda)`. Le type du propriétaire est celui de
la liaison locale ; il n'est pas écrit une seconde fois. Il n'y a ni
réécriture textuelle, ni nouvelle ABI, ni nouveau protocole de destruction.

La construction transfère `owner` exactement une fois et invalide sa liaison.
Les validations d'éligibilité, d'emprunt vivant, de capture effective, de
pureté et d'échappement sont communes avec `owningCapture`. Une lecture du
propriétaire peut produire une `Fn` réutilisable ; seule une opération du corps
qui consomme une capture produit `FnOnce`. `scoped` borne l'échappement et ne
signifie jamais « un seul appel ».

Les autres captures ambiantes gardent le contrat historique : elles ne sont
pas transférées implicitement et aucun emprunt interdit ne peut s'échapper.
Le nettoyage explicite (`using val`, `delete` ou `defer delete`) reste requis
selon les règles actuelles. Destruction de l'environnement, transfert,
désarmement et panique empruntent exclusivement les chemins existants de
`owningCapture`.

## Diagnostics et reports

Cette tranche refuse avec un diagnostic ciblé : `[]`, plusieurs éléments,
deux captures du même nom, ainsi que les modes `borrow`, `borrow var`, `mut` ou
`copy`. Plusieurs propriétaires et tout autre mode de capture sont reportés ;
les regrouper dans un propriétaire structuré explicite reste la solution
actuelle. Un identifiant inconnu, une valeur empruntée ou différée, un emprunt
encore vivant, une double capture et un usage après transfert suivent les
diagnostics d'ownership existants avec la position du nom de capture.

## Compatibilité et limites

La syntaxe historique et l'intrinsèque `owningCapture` restent acceptés,
formatés à l'identique et ne sont pas dépréciés. Aucun défaut de capture
implicite ne change. Cette tranche n'ajoute pas les listes multiples, les
captures empruntées/mutables/copiées, le nettoyage automatique de #376, une
nouvelle capacité d'appel, ni une promesse de promotion de la surface
expérimentale. Ces points restent des critères différés de #378 ou de RFC
ultérieures.

Le formateur doit préserver les deux formes et être idempotent. L'AST, ses
visiteurs et clones, l'analyse constante, le backend et le LSP doivent traiter
le préfixe comme une partie de la lambda ; hover/signature conservent le type
`Fn`/`FnMut`/`FnOnce` inféré et les variables capturées utilisent les positions
du fichier source courant, y compris avec imports, jamais des offsets d'un
module importé.
