# Flux séquentiels et buffers propriétaires

Statut : implémenté pour Janus 0.7.1.

Le module `std.io` fournit des entrées et sorties séquentielles au-dessus des
handles portables de `std.system`. Les erreurs récupérables utilisent
`Result[_, SystemError]`. Les buffers et handles de fichiers sont propriétaires
et libérés par leur destructeur.

## Buffers et texte

`ByteBuffer` et `FileData` contiennent des octets arbitraires. Ils peuvent donc
représenter une image, un protocole binaire ou du texte invalide. `isUtf8`
valide explicitement une `ByteView`; les méthodes `asText` de `ByteBuffer` et
`FileData` retournent soit une vue `string`, soit
`TextDecodeError.InvalidUtf8`.

La validation accepte les octets NUL et refuse les séquences UTF-8
sur-longues, tronquées, les substituts et les valeurs au-delà de U+10FFFF. La
vue obtenue par `asText` emprunte le stockage du buffer et reste valide
seulement tant que le `ByteBuffer` existe et n’est pas réalloué ou vidé.

## Entrées

`InputStream.read` retourne au plus le nombre d’octets demandé. Il peut
retourner moins lorsque le buffer interne ne contient qu’une partie des
données. Un résultat de taille zéro indique EOF et rend `InputStream.isEof`
vrai une fois le buffer consommé.

`InputStream.readLine` retourne un `ByteBuffer` dans une `Option` :

- l’octet LF termine la ligne et n’est pas inclus ;
- un éventuel CR précédent est conservé ;
- la dernière ligne sans LF est retournée ;
- `None` représente EOF sans donnée restante ;
- aucune validation de texte n’est implicite.

`openInputStreamBuffered` permet de choisir la capacité du buffer ;
`openInputStream` utilise 4096 octets.

## Sorties

`OutputStream.write` accepte tout le buffer fourni ou retourne une erreur. La
méthode vide son buffer en bouclant tant que l’appel natif n’a écrit qu’une
partie des octets. `OutputStream.writeText` n’ajoute ni terminaison NUL ni fin
de ligne.

`OutputStream.flush` vide d’abord le buffer Janus. Pour un fichier régulier, le
runtime demande ensuite une synchronisation native. Pour un terminal ou un
pipe, les écritures système ne possèdent pas de second buffer natif et cette
étape est un succès sans synchronisation disque. `copyStream` boucle jusqu’à
EOF et retourne le nombre total d’octets copiés.

`openOutputStreamBuffered` choisit le mode remplacement ou ajout et la capacité
du buffer ; `openOutputStream` utilise 4096 octets.

## Fermeture et flux standards

Un flux de fichier possède son handle. `InputStream.close` ou
`OutputStream.close` invalide le flux avant la fermeture native ; une seconde
fermeture ou une utilisation ultérieure produit une erreur synthétique
`InvalidInput` de code natif zéro. Le destructeur vide au mieux une sortie
encore ouverte, puis ferme son handle exactement une fois.

Depuis la révision 0.7.4, le handle invalidé est l'unique état ouvert/fermé
des deux flux. L'opération de l'erreur reste celle qui a réellement échoué
(« io.read », « io.write », « io.flush », « io.readLine » ou « io.close ») et
le contexte du flux est conservé. Aucune signature publique n'a changé.

`standardInput`, `standardOutput` et `standardError` créent des wrappers qui
possèdent leurs buffers, mais pas les handles du processus. Fermer ou détruire
un de ces wrappers ne ferme donc pas le descripteur standard sous-jacent.

## Validation et exemples

La fixture exécutable `tests/fixtures/runtime/io_streams.janus` contient deux
exemples complets :

1. copie d’un fichier avec une entrée de trois octets et une sortie de deux
   octets, afin de forcer les chemins partiels ;
2. traitement ligne par ligne avec validation UTF-8 explicite.

Elle vérifie aussi EOF, les trois flux standards, l’utilisation après
fermeture, la fermeture répétée et le nettoyage d’un répertoire temporaire
unique. Le test natif valide les handles et le flush sous Linux, macOS et
Windows. Les exécutables de compatibilité tournent sous ASan sur les runners
compatibles.

Le [benchmark des services 0.7.4](../benchmarks/stdlib-services-0.7.4.md)
verrouille le coût de la refonte avec AddressSanitizer.
