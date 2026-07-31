# Erreurs système et primitives portables

Statut : implémenté pour Janus 0.7.0.

Ce contrat constitue la frontière commune utilisée par les API de fichiers,
de flux et de processus. Les différences POSIX/Windows restent dans
le runtime C ; le code Janus reçoit toujours une valeur ou un `SystemError`
dans un `Result`.

## Modèle d'erreur

`SystemError` conserve quatre informations :

- `operation`, nom stable de l'opération Janus (`open`, `read`, `write`,
  `close` ou `remove`) ;
- `category`, catégorie portable adaptée au contrôle de flux ;
- `nativeCode`, code `errno` POSIX ou code d’erreur natif Windows ;
- `context`, chemin ou autre valeur utile fournie à l'opération.

Les catégories portables sont `NotFound`, `PermissionDenied`,
`AlreadyExists`, `InvalidInput`, `Interrupted`, `WouldBlock`, `OutOfSpace`,
`TooLarge`, `Unsupported`, `ResourceExhausted` et `Other`. La catégorie ne
remplace pas le code natif : elle permet une décision portable, tandis que le
code préserve le diagnostic propre à la plateforme.

Une fermeture répétée est une erreur `InvalidInput` synthétique dont le code
natif vaut zéro. Les autres échecs natifs conservent un code non nul quand la
plateforme en fournit un. Aucune de ces erreurs récupérables ne déclenche un
`panic`.

## Chemins et encodage

La frontière reçoit une vue `string` et sa longueur explicite :

- les chemins doivent être du UTF-8 strict, sans octet NUL embarqué ;
- POSIX transmet les octets UTF-8 au système après ajout d’un terminateur
  temporaire ;
- Windows convertit strictement vers UTF-16 avec `MultiByteToWideChar` et
  appelle uniquement les variantes larges des API ;
- une longueur qui ne peut pas être représentée par l’API native produit
  `TooLarge` ou `InvalidInput`, sans troncature silencieuse.

Les lectures et écritures portent sur des octets. Une requête supérieure à la
taille maximale d’un appel natif est bornée à cette taille et peut donc
retourner un résultat partiel. Les couches std.fs et std.io doivent boucler
explicitement lorsqu’elles exigent le traitement intégral d’un buffer.

## Ressources

`SystemFile` possède exactement un handle natif. `close()` invalide le handle
avant d’appeler le système, y compris si la fermeture native échoue. Son
destructeur ne ferme que les handles encore disponibles. Ainsi :

1. une fermeture explicite suivie de `delete` ne ferme pas deux fois ;
2. une sortie anticipée sans fermeture explicite ferme via le destructeur ;
3. toute lecture, écriture ou fermeture après invalidation retourne un
   `SystemError`.

Les modes `Read`, `Write` et `Append` signifient respectivement ouverture
existante en lecture, création/troncature en écriture et création/ajout en
écriture. `removeSystemFile` ne supprime que des fichiers ; la gestion des
répertoires et liens appartient à std.fs.

### Révision 0.7.4

Le handle natif est désormais l'unique état de disponibilité : toute valeur
négative signifie « fermé ». Il n'existe plus de booléen parallèle susceptible
de diverger du handle. `close` capture le handle, l'invalide avant l'appel
natif, puis le destructeur n'agit que si le handle est encore valide. Ce même
protocole est appliqué aux fichiers, répertoires et flux.

Cette révision ne modifie aucune signature publique. Elle conserve les buffers
et leurs longueurs explicites à la frontière native, ainsi que les quatre
champs de `SystemError`.

## Portabilité et validation

Le test natif exerce directement la même ABI sous Linux, macOS et Windows :
fichier absent, UTF-8 invalide, NUL embarqué, création, lecture, écriture
partielle possible, EOF, fermeture répétée et suppression. Un fixture Janus
vérifie le transport dans `Result`, les quatre champs de l’erreur et
l’invalidation exacte de `SystemFile`. Ces tests font partie de la matrice CI
multiplateforme.

Le [benchmark des services 0.7.4](../benchmarks/stdlib-services-0.7.4.md)
mesure aussi les cycles ouverture/écriture/fermeture/suppression de
`SystemFile`.
