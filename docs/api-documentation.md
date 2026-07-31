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

Une fiche pédagogique sépare le premier paragraphe (le résumé), les
paragraphes suivants (les détails), les paramètres, le résultat et les
exemples. La convention complète est :

````janus
/// Construit un message pour [[Printable]].
///
/// Le message peut ensuite être transmis à un moteur de rendu.
/// @param name Nom affiché dans le message.
/// @param count Nombre d’occurrences à produire.
/// @return Le message construit.
/// @example
/// ```janus
/// val message = buildMessage("Janus", 2)
/// ```
def buildMessage(name : string, count : int) : string {
    return name
}
````

Chaque `@param` doit nommer exactement un paramètre réel et chaque paramètre
public doit être documenté. Une fonction dont le résultat n’est ni `void` ni
`unit` doit fournir `@return`. Plusieurs blocs `@example` sont permis. Leur
contenu, comme tout le texte documentaire, est strictement échappé dans le
HTML. Les références `[[Nom]]` sont résolues dans le résumé, les détails, les
descriptions de paramètres et de résultat ainsi que dans les exemples.

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
exclus. Les champs historiques sont conservés. Chaque entrée expose aussi
`summary`, `details`, `parameters`, `returns` et `examples`, dans un ordre
déterministe.

`--stdlib` documente directement les sources de la bibliothèque standard
livrée avec la chaîne d’outils. Ce mode exige une documentation source pour
chaque module et chaque symbole public, refuse les liens `[[...]]` non résolus
et échoue si la couverture n’est pas complète. Il ne recherche pas de
manifeste de projet. Les contrats structurés invalides sont également des
erreurs dans ce mode. Pour un paquet, la génération reste permissive et
signale ces mêmes problèmes sous forme d’avertissements actionnables.

Le HTML contient sa feuille de style et ne charge aucune ressource réseau.
À sources, manifeste et version de Janus identiques, `index.html` et
`api-index.json` sont identiques octet par octet. Les fichiers source sont
découverts sous `src/`, triés par chemin, puis les symboles sont triés par nom
qualifié.

`--open` lance le visualiseur associé au HTML après une génération réussie.
Sous Linux, la variable standard `BROWSER` est utilisée lorsqu’elle est
définie, sinon `xdg-open`; macOS utilise `open` et Windows l’association de
fichiers du système.
