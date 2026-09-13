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

La version du paquet suit `project(VERSION ...)` à chaque configuration CMake,
y compris dans un répertoire de build réutilisé. Les anciennes entrées en cache
`JANUS_PACKAGE_VERSION` sont supprimées. Pour une nightly ou une version
personnalisée, utiliser `-DJANUS_PACKAGE_VERSION_OVERRIDE=0.24.0-nightly.example` :
CMake signale cet override et l'applique au nom d'archive, au manifeste et à
l'identité embarquée. Reconfigurer avec `-DJANUS_PACKAGE_VERSION_OVERRIDE=`
pour revenir à la version du projet.

La cible `dist` extrait l'archive produite et vérifie son nom, la version du
manifeste et l'identité JSON des trois outils avant de créer le checksum.
Cette vérification nécessite de pouvoir exécuter les binaires de la plateforme
cible sur la machine de packaging.
