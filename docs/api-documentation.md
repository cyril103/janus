# Documentation d’API

Statut : lot 0.7.2 implémenté et publié avec Janus 0.7.4.

## Commentaires publics

Un commentaire de documentation commence par `///` et s’attache à la
déclaration qui le suit. Plusieurs lignes consécutives sont réunies avec un
retour à la ligne :

```janus
/// Une valeur affichable.
/// Voir aussi [[render]].
trait Printable {
    /// Produit sa représentation textuelle.
    def render() : string
}
```

Le commentaire placé immédiatement avant `module` décrit le module. La même
syntaxe s’applique aux globales, fonctions, classes, structs, enums, variantes,
traits, champs et méthodes. Le texte est conservé dans l’AST ; `//` reste un
commentaire ordinaire sans effet documentaire.

Les références `[[Nom]]` et `[[module.Nom]]` désignent un symbole public. Un
nom court n’est lié que s’il correspond à un symbole unique. Une référence
absente ou ambiguë reste visible dans la page et produit un avertissement
portant le symbole et le contexte.

## Génération

Depuis la racine d’un paquet :

```bash
janus doc
janus doc --open
janus doc --offline -o target/reference
janus doc --stdlib --offline -o target/stdlib-reference
```

La sortie par défaut est `target/doc/index.html`. `api-index.json`, placé dans
le même dossier, fournit l’index public trié utilisé par les outils. Les
modules, types, variantes, traits, fonctions, globales et membres publics y
sont recensés ; les déclarations `private` et les membres `internal` sont
exclus.

`--stdlib` documente directement les sources de la bibliothèque standard
livrée avec la chaîne d’outils. Ce mode exige une documentation source pour
chaque module et chaque symbole public, refuse les liens `[[...]]` non résolus
et échoue si la couverture n’est pas complète. Il ne recherche pas de
manifeste de projet.

Le HTML contient sa feuille de style et ne charge aucune ressource réseau.
À sources, manifeste et version de Janus identiques, `index.html` et
`api-index.json` sont identiques octet par octet. Les fichiers source sont
découverts sous `src/`, triés par chemin, puis les symboles sont triés par nom
qualifié.

`--open` lance le visualiseur associé au HTML après une génération réussie.
Sous Linux, la variable standard `BROWSER` est utilisée lorsqu’elle est
définie, sinon `xdg-open`; macOS utilise `open` et Windows l’association de
fichiers du système.
