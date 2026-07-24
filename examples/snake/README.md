# Snake

Cet exemple complet montre comment combiner les collections et le module
`std.graphics` dans une petite application interactive. Il utilise notamment
les entrées clavier, une render texture et un fragment shader appliqué
uniquement au serpent.

Depuis la racine du dépôt :

```bash
janus run examples/snake/main.janus
```

Le backend raylib 6 doit être installé. Consultez
[`docs/graphics.md`](../../docs/graphics.md) si `initWindow` échoue.

Contrôles :

- flèches ou `ZQSD` pour se déplacer ;
- `Espace` pour mettre le jeu en pause ;
- `Entrée` pour recommencer après une collision ;
- `Échap` pour quitter.
