# Lancer et comprendre le Snake graphique

## Prérequis

- Janus 0.6.0 installé ;
- une session graphique locale ;
- **raylib 6** installée comme bibliothèque partagée ;
- le dépôt Janus cloné, car ce tutoriel exécute le vrai exemple et ses assets (`neon.fs`).

!!! warning "Backend expérimental"
    `std.graphics` et son backend raylib sont expérimentaux. Un programme Janus non graphique ne dépend pas de raylib ; celui-ci oui.

## Résultat

Le Snake néon officiel : fenêtre 800×600, flèches ou ZQSD, pause avec Espace, rendu hors écran et halo par fragment shader.

## 1. Installer raylib 6

Sous Linux, WSL ou macOS :

```bash
janus-install-raylib
```

Si la bibliothèque est ailleurs :

```bash
export JANUS_RAYLIB_PATH=/chemin/vers/libraylib.so
```

Sous Windows, définissez `JANUS_RAYLIB_PATH` vers `raylib.dll`.

## 2. Lancer l’exemple exact

Depuis la racine du dépôt :

```bash
janus check examples/snake/main.janus
janus run examples/snake/main.janus
```

Le chemin de travail est important : le programme charge `examples/snake/neon.fs`.

## 3. Lire la boucle principale

Le programme officiel initialise la fenêtre, puis garantit sa fermeture :

```janus
import std.array
import std.graphics
import std.random
import std.text

if !initWindow(800, 600, "Snake - Janus") {
    println("Erreur graphique : initialisation impossible")
    return 1
}
defer closeWindow()
setTargetFps(60)
```

À chaque image, il utilise les APIs 0.6.0 de temps et d’entrée :

```janus
while !windowShouldClose() && !isKeyPressed(Key.Escape) {
    val frameDuration : Duration = frameTime()
    val elapsedDuration : Duration = elapsedTime()

    if isKeyPressed(Key.Space) {
        paused = !paused
    }

    beginDrawing()
    clearBackground(rgb(12, 17, 27))
    // état du jeu et dessin
    endDrawing()
}
```

Le code complet conserve le serpent dans `Array[Cell]`, place la nourriture avec `automaticRandom()`, réutilise un `TextBuilder` pour le score et applique un `Shader` à une `RenderTexture`. Chaque ressource possédée reçoit un `defer delete`.

## Vérifier

- une fenêtre intitulée **Snake - Janus** apparaît ;
- flèches, ZQSD ou WASD changent la direction ;
- Espace suspend/reprend ; Entrée relance après collision ; Échap quitte ;
- l’absence de shader doit laisser le jeu jouable via le rendu de repli.

Si `initWindow` échoue, consultez `JANUS_RAYLIB_PATH` et le [guide graphique](../reference/generated/graphics.md#installer-le-backend).

## Prolongements sûrs

1. modifiez `movementDelay` dans `examples/snake/main.janus` ;
2. changez les couleurs `rgb` du plateau ;
3. remplacez le texte d’aide ;
4. relancez `janus check` avant `janus run`.

Le [source complet v0.6.0](https://github.com/cyril103/janus/blob/v0.6.0/examples/snake/main.janus) reste l’autorité : ce tutoriel n’introduit aucune API graphique supplémentaire.
