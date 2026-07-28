# Chemins et fichiers multiplateformes

Statut : implémenté pour Janus 0.7.0.

Les modules `std.path` et `std.fs` construisent l’API de haut niveau au-dessus
du contrat d’erreur de `std.system`. Les opérations récupérables retournent un
`Result` contenant un `SystemError` ; les allocations et handles appartenant à
une valeur Janus sont libérés par son destructeur.

## Chemins

`normalizePath` effectue une normalisation lexicale, sans consulter le système
de fichiers :

- les composants vides et `.` sont retirés ;
- `..` retire le composant normal précédent sans remonter au-dessus d’une
  racine absolue ;
- POSIX reconnaît `/` comme séparateur ;
- Windows accepte `/` et `\`, produit `\`, conserve les préfixes de lecteur et
  les racines UNC ;
- les entrées doivent être du UTF-8 strict sans NUL embarqué.

`joinPath` remplace la base lorsque l’enfant est absolu, sinon il joint puis
normalise les deux valeurs. Un `Path` possède le buffer UTF-8 retourné par le
runtime. Sa vue reste valide jusqu’à la destruction du `Path`. Les composants
sont accessibles par `componentCount` et `component`; chaque composant retourné
est lui-même propriétaire.

## Fichiers et écriture atomique

`readFile` lit intégralement un fichier simple et retourne un `FileData`
propriétaire. Sa vue et son pointeur restent valides jusqu’à sa destruction.
Une croissance impossible produit une erreur `TooLarge` ou
`ResourceExhausted`.

`writeFileAtomic` et `writeTextFileAtomic` :

1. créent un fichier temporaire unique dans le même répertoire que la cible ;
2. écrivent tous les octets et synchronisent le fichier ;
3. ferment le handle exactement une fois ;
4. remplacent la cible par renommage atomique.

Un échec supprime le fichier temporaire. La visibilité du remplacement est
atomique pour les lecteurs locaux ; la persistance de l’entrée de répertoire
après une perte brutale d’alimentation n’est pas garantie. Sur POSIX, le mode
du nouveau fichier dépend de `0666` et de l’umask. Sur Windows, le remplacement
utilise les API Unicode larges.

## Répertoires et métadonnées

`createDirectory` accepte une création simple ou récursive. Un répertoire déjà
présent est un succès, mais un fichier portant le même nom produit
`AlreadyExists`. `createTemporaryDirectory` crée un répertoire unique sous la
racine temporaire native ; l’appelant possède le `Path` et doit supprimer le
contenu puis le répertoire.

`readDirectory` retourne un `DirectoryIterator` propriétaire. `next` fournit
les noms immédiats, sans `.` ni `..`, dans l’ordre natif non garanti. `close`
invalide le handle avant la fermeture ; le destructeur ne ferme qu’un handle
encore ouvert. `removeDirectory` ne supprime qu’un répertoire vide.

`metadata` ne suit pas le dernier lien :

- POSIX utilise `lstat` et distingue fichier, répertoire, lien symbolique et
  autre type ;
- Windows classe tout point de réanalyse comme `SymbolicLink`, ce qui inclut
  les jonctions ;
- `readFile` suit un lien vers un fichier ;
- `removeFile` supprime le lien lui-même ;
- l’écriture atomique remplace l’entrée de lien au lieu de modifier sa cible ;
- le parcours n’est pas récursif et ne suit donc aucun lien découvert.

## Validation

Les tests natifs et Janus créent des répertoires temporaires uniques, emploient
des noms Unicode, vérifient les séparateurs natifs, remplacent atomiquement un
fichier, lisent ses octets, parcourent un répertoire et nettoient toutes les
ressources. La matrice CI exécute ces contrats sur Linux x86_64, macOS ARM64 et
Windows x86_64. Le cas de lien symbolique est exercé sur POSIX, où sa création
ne requiert pas de privilège supplémentaire.
