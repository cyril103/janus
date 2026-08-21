# Diagnostics structurés

Janus transporte ses erreurs de compilation dans un modèle commun au
compilateur et au serveur de langage. Un diagnostic contient :

- une gravité (`note`, `warning` ou `error`) ;
- un code stable dans sa famille ;
- un message ;
- une position principale ;
- zéro ou plusieurs notes et positions secondaires étiquetées ;
- zéro ou plusieurs suggestions composées d'une plage et d'un remplacement.

La sortie texte conserve la forme `fichier:ligne:colonne`, affiche le code des
diagnostics migrés entre crochets, puis l'extrait de source et son repère. Une
suggestion est uniquement affichée : Janus ne modifie jamais le fichier
automatiquement. Le LSP publie directement la gravité, le code, le message et
la position du même modèle, sans analyser la sortie destinée aux humains.

## Rendus CLI

`janus check` et `janus build` acceptent
`--diagnostic-format human|json`. Le rendu `human`, utilisé par défaut, cible
un terminal et respecte la largeur donnée par `COLUMNS`. Le rendu `json` écrit
un document unique sur la sortie d'erreur et conserve le code de sortie `1`
d'une erreur de compilation ; la sortie standard reste vide.

Le schéma [diagnostic-0.5.2.schema.json](schemas/diagnostic-0.5.2.schema.json)
décrit le document JSON. Le champ `schemaVersion` vaut `0.5.2`. Ce schéma et
ses champs restent compatibles pendant toute la série 0.5.x ; une évolution
incompatible exige la prochaine version mineure et une annonce dans le
changelog.

## Familles de codes

| Préfixe | Producteur |
|---|---|
| `JLEX` | lexer |
| `JPAR` | parser |
| `JANA` | analyse sémantique |
| `JMOD` | résolution de modules |
| `JBCK` | backend |

`J0000` identifie temporairement les anciens appels à `CompileError` qui ne
sont pas encore migrés. Un nouveau diagnostic ne doit pas utiliser ce code.

## Avertissements de sûreté sémantique

L'analyseur signale les opérations qui compilent mais dont le contrat de
propriété, de durée de vie ou de représentation mérite une vérification. Ces
avertissements ne sont pas des erreurs : ils décrivent un risque précis et
doivent être corrigés en exprimant l'intention, pas masqués mécaniquement.

| Code | Risque détecté | Réponse habituelle |
|---|---|---|
| `JANA0002` | propriétaire local encore vivant à la sortie | `delete`, `defer delete`, transfert ou retour |
| `JANA0003` | propriétaire local écrasé | nettoyer ou déplacer l'ancienne valeur |
| `JANA0004` | résultat propriétaire ignoré | conserver, transférer ou détruire le résultat |
| `JANA0005` | résultat marqué comme devant être utilisé | traiter explicitement la valeur retournée |
| `JANA0006` | champ propriétaire écrasé | extraire ou nettoyer l'ancien champ |
| `JANA0007` | destructeur incomplet | libérer tous les champs et stockages concernés |
| `JANA0008` | réallocation brute potentiellement dangereuse | employer le contrat de réallocation adapté |
| `JANA0009` | sortie anticipée sans nettoyage garanti | placer le nettoyage dans un `defer` |
| `JANA0010` | allocation croissante dans une boucle | réutiliser ou libérer à chaque itération |
| `JANA0011` | propriétaire capturé par une closure qui s'échappe | transférer explicitement avec `owningCapture` |
| `JANA0012` | cast de pointeur dont la propriété est ambiguë | conserver un alias emprunté ou documenter le transfert |
| `JANA0013` | conversion numérique susceptible de perdre de l'information | choisir `checkedCast[T]`, `saturatingCast[T]` ou `truncatingCast[T]` selon l'intention |
| `JANA0014` | valeur calculée mais inutilisée | supprimer l'expression ou exploiter son résultat |
| `JANA0015` | buffer propriétaire libéré sans nettoyage des éléments | détruire/déplacer les éléments puis `freeStorage` |
| `JANA0016` | élément propriétaire écrasé dans un pointeur | extraire ou nettoyer l'ancienne valeur |
| `JANA0017` | buffer propriétaire réalloué sans protocole | utiliser `reallocPreserving` et `adoptReallocation` |
| `JANA0018` | emprunt créé depuis un propriétaire temporaire | conserver le propriétaire dans une liaison assez longue |
| `JANA0019` | `panic` peut contourner un nettoyage manuel | enregistrer le nettoyage avec `defer` |
| `JANA0020` | paramètre pointeur `extern` sans contrat | ajouter `borrow` ou `consume` |
| `JANA0021` | cycle potentiel entre propriétaires | remplacer une arête par un champ `borrow val` |
| `JANA0022` | pointeur retourné par `extern` sans contrat | qualifier le retour `borrow` ou `owned` |

## Erreurs du système d'emprunts

Les violations d'aliasing et de durée de vie utilisent des codes distincts des
avertissements. Ces erreurs empêchent la compilation :

| Code | Violation | Réponse habituelle |
|---|---|---|
| `JANA0024` | emprunts partagé et mutable incompatibles | raccourcir une portée ou séquencer les emprunts |
| `JANA0025` | mutation, déplacement, destruction ou réallocation d'un propriétaire emprunté | terminer ou détruire la vue avant l'invalidation |
| `JANA0026` | emprunt qui s'échappe par retour, stockage ou closure | conserver l'usage dans la portée de sa source |
| `JANA0027` | opération interdite à travers un emprunt | employer `borrow def` ou demander un emprunt mutable |
| `JANA0028` | source impropre à la création d'un emprunt | lier d'abord une valeur locale propriétaire vivante |

`--warn-high-growth-loops` ajoute une analyse optionnelle des boucles dont la
croissance entière paraît involontaire. Elle est indépendante de la famille
ci-dessus et s'active sur `janus check`, `janus build` et `janus run`.

Les opérations `numericCast`, `owningCapture`, `freeStorage`,
`reallocPreserving` et `adoptReallocation` affirment une précondition au
compilateur. Elles ne rendent pas une opération sûre à l'exécution : leur usage
doit être accompagné du contrôle ou de l'invariant correspondant.
Les conversions `checkedCast`, `saturatingCast` et `truncatingCast` sont, elles,
définies sur toutes les valeurs numériques ; leur
[matrice de comportement](numeric-conversions.md) indique la politique exacte.

Avant Janus 1.0, les codes, le contenu des notes et la structure peuvent
changer entre deux versions mineures. Toute modification doit être annoncée
dans le changelog. La stabilité à partir de 1.0 sera précisée par le
[contrat de stabilité](stability-contract.md).

## Tests de non-régression

Le corpus versionné `tests/diagnostics/invalid/` associe chaque source invalide
à un code et à un fragment de message. Le test `diagnostics.invalid_corpus`
vérifie le statut contrôlé, le code et la position `fichier:ligne:colonne`.
Les fixtures `tests/diagnostics/rendering/` verrouillent le JSON et les rendus
sans couleur à 80 et 120 colonnes. Une fixture contenant deux déclarations
invalides vérifie la récupération sans cascade.

Lors du workflow nocturne, quatre campagnes indépendantes exercent le lexer, le
parser, les manifestes et le résolveur pendant 3 600 secondes chacune sous
ASan/UBSan. Toute terminaison par signal, erreur de sanitizer ou expiration
d'un cas conserve dans le log la charge utile hexadécimale permettant de le
reproduire.
