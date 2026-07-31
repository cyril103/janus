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
