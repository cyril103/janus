---
hide:
  - navigation
  - toc
---

<div class="home-shell">
  <section class="home-hero">
    <div class="eyebrow">JANUS / VERSION 0.8.1 / PRÉ-1.0</div>
    <h1>Décider si Janus vous convient.<br><em>Puis apprendre en construisant.</em></h1>
    <p class="hero-lede">Un langage compilé, fortement typé et natif, où les abstractions de haut niveau côtoient la propriété explicite, LLVM et l’interopérabilité C.</p>
    <div class="hero-actions">
      <a class="primary-action" href="book/01-premiers-pas/">Ouvrir le chapitre 1 →</a>
      <a class="quiet-action" href="reference/generated/language-guide/">Lire la référence</a>
    </div>
    <dl class="signal-line">
      <div><dt>Cible</dt><dd>Linux x86_64 · macOS ARM64 · Windows x86_64</dd></div>
      <div><dt>Sortie</dt><dd>Exécutables natifs via LLVM / Clang / LLD</dd></div>
      <div><dt>Statut</dt><dd>Pré-1.0 : API et syntaxe encore susceptibles d’évoluer</dd></div>
    </dl>
  </section>

  <aside class="decision-rail">
    <span class="rail-index">DÉCIDER / 01</span>
    <h2>Janus est pertinent si…</h2>
    <ul>
      <li>vous voulez un typage statique et des conversions explicites ;</li>
      <li>vous acceptez de gérer la durée de vie des ressources sans GC ;</li>
      <li>vous cherchez fonctions, closures, enums, traits et collections dans un langage natif ;</li>
      <li>vous pouvez travailler avec un écosystème jeune et expérimental.</li>
    </ul>
    <p><strong>Pas encore le bon choix</strong> pour une API 3D, un écosystème de paquets mature ou une garantie de stabilité avant 1.0.</p>
  </aside>
</div>

## Voir le langage, pas une promesse

<div class="code-stage" markdown>
<div class="stage-note">
<span>LEARN / 02</span>

Un programme Janus rend visibles ses contrats : types, mutabilité et code de sortie. `val` ne se réassigne pas ; `var` oui. Aucune conversion numérique implicite ne masque l’intention.
</div>

```janus title="src/main.janus"
// doctest: doctest name=homepage
def add(left : int, right : int) : int {
    return left + right
}

def main() : int {
    val initial : int = 40
    var result : int = initial
    result = add(result, 2)
    println(result)
    return 0
}
```
</div>

## Installer. Créer. Exécuter.

=== "Linux"

    ```bash
    sudo apt update && sudo apt install build-essential curl
    curl --proto '=https' --tlsv1.2 -fsSL \
      https://raw.githubusercontent.com/cyril103/janus/v0.8.1/scripts/install.sh | sh
    export PATH="$HOME/.janus/bin:$PATH"
    ```

=== "macOS ARM64"

    ```bash
    xcode-select --install
    curl --proto '=https' --tlsv1.2 -fsSL \
      https://raw.githubusercontent.com/cyril103/janus/v0.8.1/scripts/install.sh | sh
    ```

=== "Windows x86_64"

    ```powershell
    irm https://raw.githubusercontent.com/cyril103/janus/v0.8.1/scripts/install.ps1 | iex
    ```

```bash
janus --version
janus new bonjour
cd bonjour
janus run
```

## Trois profondeurs d’entrée

<div class="routes" markdown>

### 01 — Découvrir en 20 minutes
Créez un projet, lisez les diagnostics et exécutez un compteur sans absorber toute la théorie.

[Suivre le tutoriel compteur →](tutorials/cli-compteur.md)

### 02 — Apprendre dans l’ordre
Quinze chapitres vont des valeurs et fonctions à la propriété, aux dérivations, à la stdlib et au graphisme 2D, avec exercices et corrections.

[Consulter le livre →](book/index.md)

### 03 — Approfondir sans intermédiaire
La documentation canonique décrit précisément le langage 0.8.1, les outils, le texte et le graphisme.

[Entrer dans la référence →](reference/index.md)

</div>

<div class="release-note" markdown>
**Ce que 0.8.1 corrige.** L'affichage de `bool` est désormais stable à la
frontière LLVM/runtime, y compris dans une struct retournée par une fonction
et affichée au sein d'une boucle. Cette version reste compatible avec 0.8.0
et explicitement pré-1.0.
[Lire le changelog](https://github.com/cyril103/janus/blob/v0.8.1/CHANGELOG.md)
ou [la référence de la stdlib](reference/stdlib/index.html).
</div>
