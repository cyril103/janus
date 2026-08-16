# Graphisme 2D

Le module expérimental `std.graphics` fournit une API 2D typée au-dessus de
raylib 6. Le backend est chargé dynamiquement : un programme qui n'utilise pas
le graphisme ne dépend pas de raylib, et l'absence de la bibliothèque peut être
traitée avec `initWindow`.

Le contrat détaillé de propriété et de portée est consigné dans
[Propriété des ressources graphiques et audio](design/graphics-resources.md).

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

Les transitions restent explicites. Une fin sans ouverture correspondante est
ignorée, comme une seconde ouverture d'une portée drawing, caméra, render
target ou shader déjà active. `closeWindow()` termine les portées encore
actives avant de fermer la fenêtre. Ces garde-fous protègent le backend, mais
ne remplacent pas l'association locale d'un `begin` avec son `end` ou un
`defer`.

## Modes de fusion

`beginBlend()` accepte les huit modes de raylib 6 : alpha, additif,
multiplication, addition ou soustraction des couleurs, alpha prémultiplié et
les deux variantes personnalisées. `BlendMode.Alpha` conserve la composition
transparente habituelle et `BlendMode.Additive` additionne la lumière des
sprites, par exemple pour un halo. Associez toujours l'ouverture à
un `defer endBlend()` dans la même portée : le mode précédent est alors
restauré à la sortie normale, lors d'un `return` et même lorsque les portées
sont imbriquées.

```janus
if haloVisible {
    beginBlend(BlendMode.Additive)
    defer endBlend()
    haloSprite.drawAt(position, White)
}
```

Ici, seul `haloSprite` est rendu en additif ; le dessin qui suit le bloc
retrouve automatiquement le mode alpha.

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

Les primitives 2D scalaires de raylib 6 sont disponibles :

- pixels et segments ordinaires, épais, courbes ou discontinus ;
- disques, secteurs, ellipses et anneaux, pleins ou en contour ;
- rectangles simples, pivotés, en dégradé, arrondis ou en contour ;
- triangles, éventails, bandes et polygones réguliers ;
- lignes brisées et cinq familles de splines, avec dessin par tableaux,
  segments unitaires et évaluation d’un point de courbe ;
- texte avec la police par défaut ou une `Font` chargée.

Les fonctions `collisionRectangles`, `collisionCircles`,
`collisionCircleRectangle`, `collisionCircleLine` et les variantes
`collisionPoint*` exposent les tests de collision correspondants.
`collisionLines` teste deux segments, `collisionLinesPoint` renvoie leur point
d’intersection et `collisionRectangle` renvoie la zone commune à deux
rectangles. Les opérations par tableaux reçoivent un `Ptr[Vector2]` vers
`count` valeurs contiguës ; l’appelant reste propriétaire de cette mémoire.

`beginScissor(area)` limite temporairement le dessin à une zone écran ;
associez-le à `endScissor()` dans la même portée.

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

`Texture.drawEx`, `drawRegion` et `drawPro` couvrent respectivement la
rotation/échelle, une région source et le dessin complet dans un rectangle de
destination. Une largeur ou hauteur source négative retourne le sprite.
`Texture.generateMipmaps` génère les niveaux GPU, `setWrap` contrôle la
répétition hors limites et `setFilter` sélectionne notamment le filtrage
`Point` pour le pixel art ou `Bilinear` pour un redimensionnement lissé.
`drawNPatch` réalise le redimensionnement neuf-patch et `useForShapes` choisit
la région de texture employée par les primitives géométriques.

### Images CPU

`Image` est la ressource propriétaire correspondant aux pixels conservés en
RAM par raylib. Elle peut être chargée depuis un fichier, un tampon mémoire ou
le framebuffer, ou générée comme couleur unie, dégradé, damier, bruit blanc,
bruit de Perlin, cellules ou texte. `PixelFormat` expose les 24 formats raylib
6 non compressés et compressés.

```janus
val image : Image = generateImageChecked(128, 128, 8, 8, White, Blue)
defer delete image

image.drawCircle(vector2(float(64.0), float(64.0)), 24, Red)
image.flipVertical()
image.generateMipmaps()

val texture : Texture = image.toTexture()
defer delete texture
```

Les transformations couvrent format, puissance de deux, recadrage et alpha,
masque, prémultiplication, flou et convolution, redimensionnement, mipmaps,
dithering, retournement, rotation et corrections colorimétriques. Le dessin
logiciel couvre pixels, lignes, cercles, rectangles, triangles, éventails,
bandes, composition d’images et texte. `copy`, `region` et `channel` créent une
nouvelle image propriétaire ; `export` et `exportAsCode` écrivent le résultat.

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

## Temps de rendu

`frameTime()` retourne une `Duration` mesurée depuis son appel précédent (ou
depuis l'initialisation du module lors du premier appel) ; appelez-la une fois
au début de chaque image. `elapsedTime()` mesure le temps écoulé depuis cette
même initialisation. Les durées sont positives ou nulles, stockées en
nanosecondes et convertibles en microsecondes, millisecondes ou secondes.

Ces deux fonctions reposent sur l'horloge monotone de `std.time` : elles ne
reculent pas lorsque l'heure civile change et ne dépendent ni du FPS cible ni
de raylib.

```janus
val delta : Duration = frameTime()
val elapsed : double = elapsedTime().seconds()
shader.setFloat(time, float(elapsed))
```

Pour mesurer une autre opération ou utiliser le temps hors d'une application
graphique, importez directement `std.time`. Le temps civil reste disponible
séparément dans `std.wall_time`.

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
- `characterPressed` pour lire le prochain caractère Unicode selon la
  disposition active du clavier, notamment AZERTY ou QWERTY ;
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
`delete`. Depuis 0.7.4, `Vector2`, `Rectangle`, `Color` et les enums
graphiques dérivent explicitement `Copy`, `Equality` et `Debug`.

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

## Exemple complet : Snake

[`examples/snake`](../examples/snake) contient un jeu complet qui combine une
boucle interactive, les entrées clavier, un `Array` de cellules, une render
texture et un fragment shader. Le plateau est rendu normalement tandis que le
shader de halo néon s'applique uniquement au serpent.

Depuis la racine du dépôt :

```bash
janus run examples/snake/main.janus
```

## État expérimental

Le module couvre le graphisme 2D immédiat, notamment les polices personnalisées
UTF-8, les manettes, les durées de frame et les modes de fusion. Il ne fournit
pas encore :

- d'API 3D ;
- de façade sûre pour les séquences d’images animées, les palettes retournées
  par pointeur ou l’export d’image vers un tampon natif ;
- de diagnostic distinct entre un shader invalide et le shader de repli ;
- d'installation automatique de raylib par le gestionnaire de paquets.

L'API publique reste indépendante du backend afin que ces capacités puissent
être ajoutées sans exposer directement les structures natives de raylib.

## Audit et migration 0.7.4

Les six modules `std.graphics.*` restent expérimentaux. Les structures raylib,
les fonctions `janus_graphics_*` et le chargeur dynamique demeurent des détails
privés du runtime. Aucun symbole public n'est supprimé ou renommé.

Les alias historiques sont conservés :

| Alias conservé | Forme canonique | Statut |
| --- | --- | --- |
| `colorRgba` | `rgba` | compatible, non déprécié |
| `colorRgb` | `rgb` | compatible, non déprécié |
| `black()`, `white()`, `red()`, `green()`, `blue()` | constantes `Black`, `White`, `Red`, `Green`, `Blue` | compatibles |
| `clearColor` | `clearBackground` | compatible |
| helpers `*At`, `*Between` et `*Area` | primitives scalaires correspondantes | compatibles et recommandés pour les types structurés |

`Image`, `Texture`, `Font`, `RenderTexture`, `Shader`, `Sound` et `Music` restent des
classes propriétaires non copiables. Un `move` transfère leur unique handle ;
le destructeur libère le handle une fois, et un chargement invalide produit un
objet dont `isValid()` vaut `false` et dont la destruction est sans effet
natif. Les ressources graphiques doivent être détruites avant `closeWindow`,
et les ressources audio avant `closeAudio`.
