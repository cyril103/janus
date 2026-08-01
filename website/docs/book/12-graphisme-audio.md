<span class="chapter-kicker">CHAPITRE 12 / DESSINER ET ANIMER</span>
# Graphisme 2D et audio

## Objectifs

- comprendre les modules graphiques et leur backend raylib ;
- structurer une boucle de rendu ;
- distinguer formes, images CPU, textures GPU et rendus hors écran ;
- gérer entrées, caméra, shaders et audio sans fuite de ressources.

## Modules et backend

La surface graphique est répartie entre :

| Module | Responsabilité |
| --- | --- |
| `std.graphics` | façade des usages courants |
| `std.graphics.types` | couleurs, vecteurs, rectangles, enums |
| `std.graphics.drawing` | fenêtre, dessin 2D, collisions, caméra |
| `std.graphics.resources` | images, textures, polices, shaders, render textures |
| `std.graphics.input` | clavier, souris, curseur et manettes |
| `std.graphics.audio` | périphérique audio, sons et musiques |

Le runtime charge raylib 6 dynamiquement. Une compilation peut réussir sans raylib, mais l’ouverture d’une fenêtre ou d’un périphérique audio échouera proprement si le backend est absent. Consultez le [guide graphique](../reference/generated/graphics.md) pour l’installation par système.

## La boucle de rendu

```janus
// doctest: incomplete
import std.c
import std.graphics

def main() : int {
    if !initWindow(800, 450, "Janus 2D") {
        printf(cstr("Erreur graphique : %s\n"), graphicsLastError())
        return 1
    }
    defer closeWindow()
    setTargetFps(60)

    while !windowShouldClose() {
        beginDrawing()
        clearBackground(rgb(24, 32, 48))
        drawText("Bonjour Janus", 24, 24, 28, White)
        endDrawing()
    }
    return 0
}
```

Chaque `beginDrawing()` doit être associé à un `endDrawing()`. Le `defer closeWindow()` garantit la fermeture après la boucle et en cas de retour anticipé.

## Formes, splines et collisions

La surface 2D couvre pixels, lignes, cercles, secteurs, ellipses, anneaux, rectangles, triangles, polygones, dégradés et contours. Les variantes avancées acceptent épaisseur, rotation, origine ou séquences de points.

Cinq familles de splines et leurs fonctions d’évaluation sont disponibles. Les helpers de collision couvrent rectangles, cercles, points, triangles, polygones et intersections de lignes. Utilisez `Vector2` et `Rectangle` pour garder les coordonnées structurées.

## Image CPU ou texture GPU

Une `Image` vit côté CPU : elle peut être générée, recadrée, redimensionnée, tournée, recolorée, dessinée en mémoire puis exportée. Une `Texture` vit côté GPU et sert au rendu rapide dans la fenêtre.

Flux courant :

1. charger ou générer une `Image` ;
2. appliquer les transformations CPU ;
3. appeler `image.toTexture()` après l’ouverture de la fenêtre ;
4. détruire l’image si elle n’est plus utile ;
5. dessiner la texture à chaque frame ;
6. détruire la texture avant de fermer la fenêtre.

Les variantes `draw`, `drawRec`, `drawEx`, `drawPro` et N-patch couvrent position, source, destination, origine, rotation, teinte et panneaux redimensionnables. Filtres, wrapping et mipmaps règlent l’échantillonnage.

## Caméra, clipping et fusion

`Camera2D` transforme les coordonnées monde/écran. Encadrez le rendu concerné avec `beginCamera(camera)` et `endCamera()`. Les modes scissor limitent le dessin à un rectangle ; les modes de fusion contrôlent la combinaison des couleurs. Fermez toujours chaque mode dans la même portée.

## Entrées

Le clavier distingue un événement ponctuel (`isKeyPressed`) d’un état maintenu (`isKeyDown`). La souris expose position, molette, boutons et contrôle du curseur. Les manettes fournissent disponibilité, nom, boutons, axes et vibration.

Mettez à jour la simulation depuis l’état d’entrée avant le dessin. Utilisez le temps de frame pour les mouvements continus, pas le nombre de frames, afin que la vitesse reste stable.

## Polices, shaders et rendu hors écran

Une `Font` dessine et mesure du texte UTF-8 à une taille et un espacement choisis. Un `RenderTexture` permet de dessiner dans une texture, puis de la composer dans la fenêtre. Un `Shader` charge les programmes GPU disponibles et s’active dans un bloc begin/end.

Ces trois types sont propriétaires. Leur création dépend d’un contexte graphique actif et leur destruction doit intervenir avant `closeWindow()`.

## Audio

Initialisez le périphérique avec `initAudio()`, puis fermez-le avec `closeAudio()`. `Sound` convient à un effet court chargé en mémoire ; `Music` est mise à jour en streaming et nécessite `update()` dans la boucle.

```janus
// doctest: incomplete
if initAudio() {
    defer closeAudio()
    val effect : Sound = loadSound("click.wav")
    defer delete effect
    if effect.isValid() {
        effect.play()
    }
}
```

Volume, pitch et panoramique sont réglables. Détruisez sons et musiques avant de fermer le périphérique audio.

## État de la couverture

La surface principale de rendu raylib 2D est largement exposée, notamment les formes, collisions, images CPU et textures. Cela ne signifie pas encore « tout raylib hors 3D » : certaines API avancées d’animation d’image, buffers natifs, texte/polices, entrées tactiles/gestuelles, streaming audio et uniformes de shaders restent hors de la façade sûre actuelle.

## Exercice

Décrivez l’ordre de destruction correct d’une image transformée en texture dans une fenêtre.

??? success "Correction"
    Après la création de la texture, l’image CPU peut être détruite. À la sortie, détruisez d’abord la texture GPU, puis appelez `closeWindow()`. Les `defer` doivent donc être enregistrés dans l’ordre fenêtre, texture : ils s’exécuteront en ordre inverse.

Poursuivez avec le [Snake graphique](../tutorials/snake-graphique.md) et la [référence graphique complète](../reference/generated/graphics.md).

<div class="lesson-nav"><a href="../11-bibliotheque-standard/">← Bibliothèque standard</a><a href="../13-projets-tests-outils/">Projets, tests et outils →</a></div>
