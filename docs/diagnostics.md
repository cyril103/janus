# Diagnostics structurés

Janus transporte ses erreurs de compilation dans un modèle commun au
compilateur et au serveur de langage. Un diagnostic contient :

- une gravité (`note`, `warning` ou `error`) ;
- un code stable dans sa famille ;
- un message ;
- une position principale ;
- zéro ou plusieurs notes et positions secondaires étiquetées.

La sortie texte conserve la forme `fichier:ligne:colonne` et affiche le code
des diagnostics migrés entre crochets. Le LSP publie directement la gravité,
le code, le message et la position du même modèle, sans analyser la sortie
destinée aux humains.

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

Lors du workflow nocturne, deux campagnes de mutation indépendantes exercent
le lexer et le parser pendant au moins quinze minutes chacune. Toute
terminaison par signal, erreur de sanitizer ou expiration d'un cas conserve
dans le log la charge utile hexadécimale permettant de le reproduire.
