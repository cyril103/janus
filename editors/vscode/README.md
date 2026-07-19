# Extension VS Code pour Janus

Cette extension associe les fichiers `.janus` au langage Janus, fournit une
coloration syntaxique et démarre `janus-lsp` sur l'entrée/sortie standard.

Le serveur est recherché dans cet ordre :

1. le paramètre VS Code `janus.server.path` ;
2. `$JANUS_HOME/bin` ;
3. `~/.janus/bin` ;
4. le `PATH`.

Pour développer ou construire un paquet VSIX :

```bash
cd editors/vscode
npm install
npm run package
```
