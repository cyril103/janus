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

Entrées disponibles :

- `isKeyDown` et `isKeyPressed` avec l'enum `Key` ;
- `mouseX` et `mouseY` ;
- `isMouseButtonDown` et `isMouseButtonPressed` avec `MouseButton`.

Le MVP expose les touches `Space`, `Enter`, `Escape`, les flèches et les
touches `W`, `A`, `S`, `D`.

## État expérimental

Cette première version vise le graphisme 2D immédiat. Elle ne fournit pas encore
les textures, les polices personnalisées, l'audio, les manettes, la 3D ni le
chargement automatique de raylib par le gestionnaire de paquets. L'API publique
reste indépendante du backend afin de pouvoir ajouter ces fonctionnalités sans
exposer directement les structures natives de raylib.
