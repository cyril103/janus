# Limites connues de Janus 0.8

Janus 0.8 reste une version pré-1.0. Les surfaces ci-dessous sont conservées
pour expérimentation, mais ne font pas partie du candidat stable.

- `manifest.dependencies.registry`, `resolver.registry`, `cli.search` et
  `cli.publish` : le protocole v1 est versionné, mais le client, la politique de
  cache et l'expérience de publication distante nécessitent davantage de recul.
- `tooling.janus-lsp` et `tooling.vscode` : une matrice de compatibilité est
  publiée, mais les garanties comportementales et l'interface éditeur peuvent
  encore évoluer avant leur gel explicite dans le contrat 1.x.
- `std.graphics`, `std.graphics.audio`, `std.graphics.drawing`,
  `std.graphics.input`, `std.graphics.resources` et `std.graphics.types` :
  l'API dépend de raylib et sa portabilité, sa gestion de ressources et sa
  couverture tier-1 ne satisfont pas encore le niveau du noyau de la stdlib.

Autres limites hors surface promise : format et ordre exacts des diagnostics,
ABI des agrégats Janus, objets compilés entre plateformes, performances,
contenu du cache et API C++ internes du compilateur. Elles sont détaillées dans
le [contrat proposé](stability-contract.md).
