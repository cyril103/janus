# Identité de la toolchain

`janus`, `janus-lsp` et `janusup` partagent une identité de build. La commande
`--version` reste lisible par un humain ; `--version --json` fournit le contrat
machine-readable de schéma 1 avec `version`, `display_version`, `revision`,
`dirty`, `channel`, `identity`, `target` et `llvm`.

Le canal `stable` désigne uniquement un checkout propre placé exactement sur le
tag `v<version>`, y compris pour son archive, et conserve la SemVer canonique.
Un build local post-tag utilise `source` et inclut le SHA ; un worktree modifié
ajoute `.dirty` et un digest de son contenu afin que deux états sales du même
commit ne partagent pas le cache. Les snapshots et builds sans Git utilisent `package` : la CI
injecte obligatoirement le SHA complet avec `JANUS_SOURCE_SHA`, ce qui permet de
reconstruire sans répertoire `.git`. Le fichier correspondant est
installé dans `share/janus/build-identity.json` et lie ainsi binaires, stdlib,
archive, checksum et attestation au même commit.

L’identité complète, et non la seule SemVer, participe au fingerprint du cache
incrémental. Deux compilateurs ou stdlibs issus de révisions différentes ne
peuvent donc pas partager une entrée compatible.
