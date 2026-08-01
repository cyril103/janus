# Propriété des ressources graphiques et audio

Statut : implémenté pour Janus 0.7.4, surface expérimentale.

Ce contrat couvre les wrappers de `std.graphics.*` et leur frontière avec le
runtime raylib dynamique. Les structures raylib et les symboles
`janus_graphics_*` restent privés ; le code Janus manipule des valeurs
structurelles ou des classes propriétaires.

## Valeurs et ressources

`Vector2`, `Rectangle`, `NPatchInfo`, `Color`, `Key`, `MouseButton`,
`TextureFilter`, `TextureWrap`, `PixelFormat`, `NPatchLayout`, `BlendMode`,
`GamepadButton` et `GamepadAxis` dérivent explicitement `Copy`, `Equality` et
`Debug`. Leur copie ne possède aucun handle et n'exécute aucun nettoyage.

Les sept classes suivantes sont non copiables :

| Classe | Ressource native |
| --- | --- |
| `Image` | pixels et mipmaps conservés en RAM |
| `Texture` | texture GPU |
| `Font` | atlas et données de fonte |
| `RenderTexture` | cible, texture couleur et profondeur |
| `Shader` | programme et emplacements d'uniformes |
| `Sound` | buffer audio court |
| `Music` | flux audio progressif |

Chaque instance contient un unique handle opaque. `move` transfère l'instance
sans dupliquer ce handle. Son destructeur appelle une seule fonction de
libération runtime ; le runtime vérifie la validité native, libère la ressource
raylib lorsqu'elle existe, puis libère le wrapper opaque.

Un chargement invalide retourne un handle nul pour les sept familles.
L'instance Janus reste destructible, `isValid()` retourne `false` et aucune
fonction de déchargement raylib n'est appelée. Les ressources GPU sont
détruites avant `closeWindow`, et les ressources audio avant `closeAudio`.

## Portées de rendu

Les paires restent visibles dans l'API :

- `beginDrawing` / `endDrawing` ;
- `beginCamera` / `endCamera` ;
- `RenderTexture.begin` / `endRenderTexture` ;
- `Shader.begin` / `endShader` ;
- `beginBlend` / `endBlend`.
- `beginScissor` / `endScissor`.

Le runtime conserve un état par portée non imbriquable. Une seconde ouverture
drawing, caméra, render target ou shader est ignorée ; une fin sans ouverture
l'est aussi. Les modes de fusion gardent leur pile bornée existante afin de
restaurer le mode précédent. `closeWindow` termine toute portée encore active
avant de fermer le backend.

Ces garde-fous rendent un déséquilibre sans danger pour la frontière native,
mais ne changent pas l'usage recommandé : ouvrir et fermer dans la même portée,
avec `defer` lorsqu'une sortie anticipée est possible.

## Compatibilité et alias

La révision ne retire aucun symbole des six modules graphiques et ajoute la
surface 2D raylib 6 de façon additive.
`colorRgba`, `colorRgb`, les fonctions de couleurs nommées, `clearColor` et les
helpers `*At`, `*Between` ou `*Area` restent des délégations compatibles. Ils ne
sont ni dépréciés ni exposés au runtime comme seconds symboles natifs.

La surface entière demeure expérimentale jusqu'à la définition d'un contrat
d'erreur complet et à sa promotion explicite. Les détails raylib peuvent
évoluer sans modifier les types et fonctions Janus inventoriés.

## Validation

Le backend factice compte chaque déchargement et avorte sur un double
déchargement, une portée native répétée ou une fin native sans ouverture. La
fixture Janus déplace les ressources, détruit certaines explicitement et
d'autres lors d'une sortie anticipée, puis vérifie les chargements invalides.
Elle s'exécute sous AddressSanitizer et UndefinedBehaviorSanitizer.

Les garde-fous de portée ajoutent uniquement des tests et affectations en temps
constant, sans allocation sur les chemins de dessin. Les opérations `Image`
allouent seulement lorsque raylib crée ou copie une ressource propriétaire.
