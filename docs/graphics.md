# Graphisme 2D

Le module expérimental `std.graphics` fournit une API 2D typée au-dessus de
raylib 6. Le backend est chargé dynamiquement : un programme qui n'utilise pas
le graphisme ne dépend pas de raylib, et l'absence de la bibliothèque peut être
traitée avec `initWindow`.

`std.graphics` est une façade qui regroupe les sous-modules `types`, `drawing`,
`resources`, `audio` et `input`. Les symboles natifs restent privés dans le
sous-module qui les utilise.

## Installer le backend

Sous Linux, WSL et macOS, l'outil livré avec Janus installe les dépendances,
compile raylib 6 comme bibliothèque partagée puis l'installe dans
`/usr/local` :

```bash
janus-install-raylib
```

Pour installer Janus et raylib en une seule commande, activez l'option de
l'installateur :

```bash
curl --proto '=https' --tlsv1.2 -sSf \
  https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.sh |
  JANUS_INSTALL_RAYLIB=1 sh
```

Le script accepte `--prefix`, `--skip-dependencies` et `--dry-run`. La révision
raylib correspondant à la version 6.0 est épinglée pour rendre la construction
reproductible. Sous Windows, installez pour l'instant `raylib.dll` séparément.
Vous pouvez aussi compiler raylib depuis les sources en suivant la
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
        drawCircle(400, 225, float(80.0), Blue)
        drawText("Bonjour depuis Janus !", 245, 40, 28, White)
        endDrawing()
    }
    return 0
}
```

`beginDrawing()` et `endDrawing()` doivent encadrer les commandes de rendu de
chaque image. `defer closeWindow()` garantit la fermeture de la fenêtre lors
d'un `return`.

## Couleurs

Une couleur est représentée par le struct `Color`. Utilisez les constructeurs
suivants :

```janus
val opaqueOrange : Color = rgb(255, 161, 0)
val translucentBlue : Color = rgba(0, 121, 241, 128)
```

Les couleurs prédéfinies sont les valeurs globales typées `Black`, `White`,
`Red`, `Green` et `Blue`. Les fonctions `black()`, `white()`, `red()`,
`green()` et `blue()` retournent également un `Color`.

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
    sprite.draw(100, 80, White)
}
```

`width()` et `height()` donnent les dimensions chargées. Les formats pris en
charge sont ceux activés dans la construction de raylib, notamment PNG, JPEG,
BMP, TGA et QOI dans la configuration standard.

### Sprites avancés

`Texture.drawPro` dessine une région source dans un rectangle destination avec
une origine et une rotation. Une largeur ou hauteur source négative retourne le
sprite. `Texture.setFilter` sélectionne notamment le filtrage `Point` pour le
pixel art ou `Bilinear` pour un redimensionnement lissé.

Une spritesheet régulière peut être animée avec `SpriteAnimation` :

```janus
sprite.setFilter(TextureFilter.Point)
val animation : SpriteAnimation = new SpriteAnimation(
    sprite, 16, 16, 4, 8, 0
)
defer delete animation

animation.draw(position, float(3.0), float(0.0), false, false, tint)
animation.advance()
```

Les paramètres indiquent la texture, la largeur et hauteur d'une image, le
nombre de colonnes, le nombre total d'images et l'image initiale. L'animation
référence la texture sans en prendre possession : elle doit donc être détruite
avant la texture.

## Polices et texte UTF-8

Une police personnalisée est une ressource possédée :

```janus
val font : Font = loadFontUtf8(
    "assets/Inter-Regular.ttf",
    32,
    "Bonjour, 世界 !"
)
defer delete font

if font.isValid() {
    val position : Vector2 = vector2(float(40.0), float(60.0))
    val size : Vector2 = font.measure(
        "Bonjour, 世界 !",
        float(32.0),
        float(1.0)
    )
    font.draw(
        "Bonjour, 世界 !",
        position,
        float(32.0),
        float(1.0),
        colorRgb(240, 240, 240)
    )
}
```

`loadFontUtf8` construit l'atlas avec les caractères UTF-8 indiqués dans son
troisième argument. La police source doit contenir ces glyphes ; sinon raylib
utilise son glyphe de secours. `loadFont` charge seulement le jeu de caractères
par défaut. `Font.draw` et `Font.measure` décodent ensuite les chaînes UTF-8,
et `measure` renvoie un `Vector2` par valeur.

## Rendu hors écran et shaders

`RenderTexture` permet de dessiner dans une surface indépendante, par exemple
pour produire une image pixel art en basse résolution :

```janus
val target : RenderTexture = loadRenderTexture(320, 180)
defer delete target

target.begin()
clearBackground(Black)
drawCircle(160, 90, float(24.0), Red)
endRenderTexture()
```

La texture obtenue se dessine avec `target.drawPro`. Pour l'afficher à
l'endroit, utilisez une hauteur source négative, car les coordonnées des
textures de rendu sont inversées verticalement.

Un shader de fragment utilise le shader de sommets par défaut :

```janus
val shader : Shader = loadFragmentShader("assets/post.fs")
defer delete shader
val time : int = shader.location("time")
shader.setFloat(time, elapsed)

shader.begin()
target.drawPro(source, destination, origin, float(0.0), whiteColor)
endShader()
```

`loadShader` accepte aussi un shader de sommets et un shader de fragments.
Les uniforms sont résolus avec `location`, puis configurés avec `setFloat`,
`setInt`, `setVector2` ou `setColor`. Une localisation `-1` est ignorée sans
erreur, ce qui permet au compilateur GLSL d'éliminer un uniform inutilisé.

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

### Manettes

Les manettes utilisent un index, généralement `0` pour la première :

```janus
if isGamepadAvailable(0) {
    val horizontal : float = gamepadAxis(0, GamepadAxis.LeftX)
    if isGamepadButtonPressed(0, GamepadButton.RightFaceDown) {
        setGamepadVibration(0, float(0.4), float(0.4), float(0.15))
    }
}
```

`GamepadButton` couvre les directions, boutons d'action, gâchettes, boutons
centraux et sticks. `GamepadAxis` couvre les deux sticks et les deux gâchettes
analogiques. `gamepadAxisCount`, `gamepadButtonPressed` et `gamepadName`
permettent l'inspection bas niveau ; ce dernier renvoie une chaîne C
`Ptr[byte]`, utilisable notamment avec `printf`.

## Primitives typées

`Vector2`, `Rectangle` et `Color` évitent de mélanger accidentellement des
coordonnées et des couleurs :

```janus
val center : Vector2 = vector2(float(320.0), float(180.0))
val panel : Rectangle = rectangle(
    float(20.0),
    float(20.0),
    float(600.0),
    float(320.0)
)
val accent : Color = colorRgb(0, 121, 241)

drawRectangleArea(panel, accent)
drawCircleAt(center, float(32.0), accent)
```

Les helpers `drawPixelAt`, `drawLineBetween`, `drawCircleAt`,
`drawRectangleArea`, `drawTextAt`, `clearColor` et `Texture.drawAt` utilisent
ces types. Les fonctions à coordonnées et couleurs brutes restent disponibles
pour préserver la compatibilité. Ces primitives sont des `struct` copiés par
valeur : elles ne font aucune allocation et ne doivent pas être libérées avec
`delete`.

## Caméra 2D

Une `Camera2D` définit le point visé, son décalage à l'écran, sa rotation et
son zoom :

```janus
val camera : Camera2D = new Camera2D(
    float(400.0),
    float(225.0),
    float(0.0),
    float(0.0),
    float(0.0),
    float(2.0)
)
defer delete camera

beginCamera(camera)
drawCircle(0, 0, float(24.0), Red)
endCamera()
```

`screenToWorld` et `worldToScreen` convertissent un `Vector2` entre les deux
repères. Le résultat est une valeur autonome sans allocation.

## État expérimental

Cette première version vise le graphisme 2D immédiat. Elle ne fournit pas encore
les polices personnalisées, les manettes, la 3D ni le
chargement automatique de raylib par le gestionnaire de paquets. L'API publique
reste indépendante du backend afin de pouvoir ajouter ces fonctionnalités sans
exposer directement les structures natives de raylib.
