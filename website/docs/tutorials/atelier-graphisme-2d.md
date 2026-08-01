# Atelier : explorer la surface graphique 2D

## Prérequis

- avoir installé raylib 6 comme indiqué dans le [guide graphique](../reference/generated/graphics.md) ;
- connaître les règles de [propriété](../book/09-propriete-avancee.md) ;
- avoir lu le [chapitre graphisme et audio](../book/12-graphisme-audio.md).

## Résultat

Nous allons construire une petite scène qui montre plusieurs formes, fait suivre un disque à la souris, détecte une collision et affiche une texture générée côté CPU.

## 1. Ouvrir la fenêtre

```janus
import std.c
import std.graphics

def main() : int {
    if !initWindow(960, 540, "Atelier Janus 2D") {
        printf(cstr("Erreur graphique : %s\n"), graphicsLastError())
        return 1
    }
    defer closeWindow()
    setTargetFps(60)

    while !windowShouldClose() {
        beginDrawing()
        clearBackground(rgb(20, 24, 34))
        drawText("Atelier 2D", 24, 20, 30, White)
        endDrawing()
    }
    return 0
}
```

Testez ce socle avant d’ajouter des ressources. Si la fenêtre ne s’ouvre pas, le message provient du backend chargé dynamiquement.

## 2. Dessiner des formes avancées

Dans la boucle, après `clearBackground`, ajoutez :

```janus
// doctest: incomplete
val panel : Rectangle = rectangle(
    float(40.0), float(90.0), float(400.0), float(360.0)
)
drawRectangleGradientVertical(panel, rgb(42, 54, 88), rgb(18, 22, 38))
drawRectangleRoundedLines(
    panel, float(0.08), 12, float(3.0), rgb(80, 190, 255)
)

val center : Vector2 = vector2(float(680.0), float(180.0))
drawCircleGradient(
    center, float(72.0), rgb(255, 230, 70), rgb(255, 120, 30)
)
drawRing(
    center,
    float(88.0),
    float(104.0),
    float(20.0),
    float(320.0),
    48,
    rgb(245, 80, 170)
)

drawPolygon(
    vector2(float(680.0), float(390.0)),
    6,
    float(82.0),
    float(30.0),
    rgb(155, 90, 240)
)
```

Les constructeurs `vector2`, `rectangle`, `rgb` et `rgba` évitent de manipuler les représentations internes. Les angles sont exprimés en degrés pour les formes qui les acceptent.

## 3. Ajouter une collision interactive

Construisez le centre du pointeur depuis la souris, puis testez son disque contre le panneau :

```janus
// doctest: incomplete
val pointer : Vector2 = vector2(float(mouseX()), float(mouseY()))
val touching : bool = collisionCircleRectangle(pointer, float(18.0), panel)

if touching {
    drawCircleAt(pointer, float(18.0), Green)
    drawText("collision", 60, 410, 22, Green)
} else {
    drawCircleAt(pointer, float(18.0), Red)
}
```

`isMouseButtonPressed` convient à une action ponctuelle ; `isMouseButtonDown` convient à un état continu. La même distinction existe entre `isKeyPressed` et `isKeyDown`.

## 4. Générer une texture sans fichier

Après l’ouverture de la fenêtre, mais avant la boucle :

```janus
// doctest: incomplete
val image : Image = generateImageChecked(
    128,
    128,
    16,
    16,
    rgb(40, 170, 220),
    rgb(18, 42, 70)
)
val texture : Texture = image.toTexture()
delete image
defer delete texture
```

L’image CPU n’est plus nécessaire une fois transférée au GPU. La texture, elle, doit rester vivante pendant la boucle et être détruite avant `closeWindow()`. L’ordre d’enregistrement des `defer` garantit cela.

Dans le dessin :

```janus
// doctest: incomplete
texture.drawEx(
    vector2(float(470.0), float(90.0)),
    float(8.0),
    float(1.25),
    White
)
```

## 5. Choisir l’API adaptée

| Objectif | API à chercher |
| --- | --- |
| trait épais, courbe ou pointillé | `drawLineEx`, `drawLineBezier`, `drawLineDashed` |
| arc ou anneau | `drawCircleSector`, `drawRing` et variantes `Lines` |
| rectangle tourné ou arrondi | `drawRectanglePro`, `drawRectangleRounded` |
| chemin lissé | fonctions `drawSpline*` et `splinePoint*` |
| collision | fonctions `collision*` |
| découpage | `beginScissor` / `endScissor` |
| mélange de couleurs | `beginBlend` / `endBlend` |
| monde défilant | `Camera2D`, `beginCamera` / `endCamera` |
| sprite transformé | `Texture.drawRec`, `drawEx`, `drawPro`, `drawNPatch` |
| traitement CPU | méthodes de `Image`, puis `toTexture()` |
| rendu intermédiaire | `RenderTexture` |
| effet GPU | `Shader` |

## Vérifier

```bash
janus fmt
janus check
janus run
```

Fermez la fenêtre plusieurs fois et surveillez les diagnostics : une séquence begin/end déséquilibrée ou une ressource GPU détruite après la fenêtre doit être corrigée.

## Prolongements

- Faites glisser une forme seulement pendant un bouton maintenu.
- Construisez quatre points et dessinez une spline Catmull–Rom.
- Dessinez la scène dans un `RenderTexture`, puis appliquez un shader.
- Ajoutez un effet `Sound` lors de l’entrée en collision.
- Comparez votre boucle à celle du [Snake graphique](snake-graphique.md).
