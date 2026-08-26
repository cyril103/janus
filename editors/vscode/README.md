# Extension VS Code pour Janus

Cette extension associe les fichiers `.janus` au langage Janus, fournit la
coloration syntaxique et démarre `janus-lsp` sur l'entrée/sortie standard. Les
diagnostics structurés affichent leur code, leurs notes, leurs emplacements
associés et leurs corrections. Les actions rapides couvrent notamment les
corrections sûres proposées par le compilateur, les imports manquants non
ambigus et les branches de `match` manquantes.

## Installation et mise à jour

Sur la Marketplace, rechercher **Janus Language**, puis choisir **Install**.
VS Code installe ensuite les mises à jour normalement. Un
VSIX tagué peut aussi être installé ou mis à jour sans Marketplace :

```bash
code --install-extension janus-language.vsix
```

La même commande avec un VSIX plus récent met l'installation existante à jour.
Après une installation ou une mise à jour, ouvrir un fichier `.janus` et
contrôler la sortie **Janus Language Server**.

## Choix de `janus-lsp`

Le serveur est recherché dans cet ordre déterministe :

1. le paramètre VS Code `janus.server.path` ;
2. `$JANUS_HOME/bin` (y compris le canal sélectionné par `janusup`) ;
3. `~/.janus/bin` ;
4. le `PATH`.

Un chemin explicite a donc toujours priorité. Après un changement de chemin ou
de canal `janusup`, exécuter **Developer: Reload Window**. Si aucun exécutable
n'est trouvé, l'extension propose d'ouvrir directement le paramètre concerné.

## Compatibilité

| Extension | `janus-lsp` | VS Code | Statut |
| --- | --- | --- | --- |
| 0.7.4 | 0.7.4 | 1.91 ou plus récent | VSIX historique du dépôt |
| 0.7.6 | 0.7.5–0.7.6 | 1.91 ou plus récent | ancienne version Marketplace |
| 0.8.0 | 0.8.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.8.1 | 0.8.1 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.9.0 | 0.9.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.11.1 | 0.11.1 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.12.0 | 0.12.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.13.0 | 0.13.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.14.0 | 0.14.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.19.0 | 0.19.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.20.0 | 0.20.0 | 1.91 ou plus récent | ancienne version Marketplace, pré-1.0 |
| 0.21.0 | 0.21.0 | 1.91 ou plus récent | version courante, remise Marketplace manuelle, pré-1.0 |

La version d'extension et la toolchain du même tag sont la combinaison
recommandée. Les capacités LSP sont négociées à l'initialisation : une
toolchain plus ancienne continue de fournir ses capacités connues, sans les
nouvelles actions.

## Développement et paquet VSIX

```bash
cd editors/vscode
npm ci
npm test
npm run package -- --out janus-language.vsix
```

`npm test` protège la priorité du chemin configuré ainsi que les dispositions
d'installation et de mise à jour gérées par `JANUS_HOME`. La CI reconstruit
ensuite le bundle et le VSIX depuis le lockfile.

## Préparation et remise manuelle pour la Marketplace

Pour chaque tag stable `vX.Y.Z`, le workflow **Prepare VS Code extension**
checkout exactement le tag, vérifie que le tag, `package.json` et
`package-lock.json` portent la même version, puis exécute l'installation depuis
le lockfile, les tests et la création du VSIX. Il liste le contenu du paquet et
dépose `janus-language.vsix` avec `janus-language.vsix.sha256` dans l'artifact
de workflow `janus-vscode-vX.Y.Z`. Le déclenchement manuel accepte aussi un tag
stable existant et reconstruit exclusivement son contenu.

`janus-language.vsix` et son fichier `.sha256` restent aussi attachés à la
GitHub Release principale par le workflow de release. Le mainteneur chargé de
la publication Marketplace peut donc télécharger les deux fichiers depuis cette
release ou, pour une reconstruction dédiée, depuis l'artifact **Prepare VS Code
extension**. Depuis leur dossier de téléchargement, il vérifie impérativement
l'empreinte avant l'upload :

```bash
sha256sum --check janus-language.vsix.sha256
```

La mise en ligne sur Visual Studio Marketplace appartient exclusivement au
mainteneur autorisé et reste manuelle. Après validation du checksum, la commande
informative qu'il peut exécuter lui-même avec ses propres accès locaux est :

```bash
npx vsce publish --packagePath janus-language.vsix
```

Le dépôt et la CI ne configurent, ne lisent et n'exigent aucun identifiant
Marketplace. Une version déjà acceptée ne doit jamais être remplacée : il faut
corriger, incrémenter la version et créer un nouveau tag.
