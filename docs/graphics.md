# Graphisme 2D

Le module expérimental `std.graphics` fournit une API 2D typée au-dessus de
raylib 6. Le backend est chargé dynamiquement : un programme qui n'utilise pas
le graphisme ne dépend pas de raylib, et l'absence de la bibliothèque peut être
traitée avec `initWindow`.

## Installer le backend

Installez la bibliothèque partagée raylib 6 pour votre système, ou compilez-la
depuis les sources en suivant la
[documentation officielle](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux).

Janus recherche automatiquement :

- `raylib.dll` sous Windows ;
- `libraylib.6.0.dylib`, `libraylib.600.dylib` ou `libraylib.dylib` sous
  macOS ;
- `libraylib.so.600`, `libraylib.so.6.0` ou `libraylib.so` sous Linux.

Si la bibliothèque se trouve ailleurs, indiquez son chemin exact :

```bash
export JANUS_RAYLIB_PATH=/chemin/vers/libraylib.so
janus run examples/graphics.janus
```

Sous PowerShell :

```powershell
$env:JANUS_RAYLIB_PATH = "C:\chemin\vers\raylib.dll"
janus run examples/graphics.janus
```

## Première fenêtre

```janus
import std.c
import std.graphics

def main() : int {
    if !initWindow(800, 450, "Janus Graphics") {
        printf(cstr("Erreur graphique : %s\n"), graphicsLastError())
        return 1
    }
    defer closeWindow()
    setTargetFps(60)

    while !windowShouldClose() {
        beginDrawing()
        clearBackground(rgb(24, 28, 36))
        drawCircle(400, 225, float(80.0), blue())
        drawText("Bonjour depuis Janus !", 245, 40, 28, white())
        endDrawing()
    }
    return 0
}
```

`beginDrawing()` et `endDrawing()` doivent encadrer les commandes de rendu de
chaque image. `defer closeWindow()` garantit la fermeture de la fenêtre lors
d'un `return`.

## Couleurs

Une couleur est représentée par un `uint` opaque au format RGBA. Utilisez les
constructeurs plutôt que de dépendre de sa représentation :

```janus
val opaqueOrange : uint = rgb(255, 161, 0)
val translucentBlue : uint = rgba(0, 121, 241, 128)
```

Les couleurs prédéfinies du MVP sont `black()`, `white()`, `red()`, `green()`
et `blue()`.

## Dessin et entrées

Commandes de dessin disponibles :

- `clearBackground`, `drawPixel` et `drawLine` ;
- `drawCircle` et `drawRectangle` ;
- `drawText`.

Les textures sont des ressources possédées et doivent être libérées :

```janus
val sprite : Texture = loadTexture("assets/sprite.png")
defer delete sprite

if sprite.isValid() {
    sprite.draw(100, 80, white())
}
```

`width()` et `height()` donnent les dimensions chargées. Les formats pris en
charge sont ceux activés dans la construction de raylib, notamment PNG, JPEG,
BMP, TGA et QOI dans la configuration standard.

## Audio

Initialisez le périphérique audio une fois, puis chargez des effets courts avec
`Sound` ou de la musique diffusée progressivement avec `Music` :

```janus
if initAudio() {
    defer closeAudio()

    val effect : Sound = loadSound("assets/jump.wav")
    defer delete effect
    effect.play()

    val music : Music = loadMusic("assets/theme.ogg")
    defer delete music
    music.play()

    while !windowShouldClose() {
        music.update()
        // dessin de l'image
    }
}
```

Une musique doit recevoir `update()` à chaque image. `Sound` et `Music`
proposent `play`, `stop`, `isPlaying`, `setVolume`, `setPitch` et `setPan`.
`setMasterVolume` règle le volume global.

Entrées disponibles :

- `isKeyDown` et `isKeyPressed` avec l'enum `Key` ;
- `keyPressed` pour lire la prochaine touche saisie ;
- `mouseX`, `mouseY`, `setMousePosition` et `mouseWheelMove` ;
- `isMouseButtonDown` et `isMouseButtonPressed` avec `MouseButton`.
- `showCursor`, `hideCursor`, `enableCursor`, `disableCursor` et
  `isCursorHidden`.

L'enum `Key` couvre les lettres, chiffres, ponctuation, flèches, touches de
fonction, pavé numérique et modificateurs. `MouseButton` couvre les sept
boutons reconnus par raylib.

La fenêtre peut être inspectée avec `isWindowFullscreen`, `isWindowHidden`,
`isWindowMinimized`, `isWindowMaximized`, `isWindowFocused` et
`isWindowResized`. Les fonctions `toggleFullscreen`, `maximizeWindow`,
`minimizeWindow`, `restoreWindow`, `setWindowTitle`, `setWindowPosition`,
`setWindowSize` et `setWindowOpacity` la contrôlent. `screenWidth` et
`screenHeight` donnent la taille courante de la zone de dessin.

## État expérimental

Cette première version vise le graphisme 2D immédiat. Elle ne fournit pas encore
les polices personnalisées, les manettes, la 3D ni le
chargement automatique de raylib par le gestionnaire de paquets. L'API publique
reste indépendante du backend afin de pouvoir ajouter ces fonctionnalités sans
exposer directement les structures natives de raylib.
