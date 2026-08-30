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
le même dossier, fournit l’index public trié utilisé par la documentation, le
CLI et le LSP. Son contrat `format_version: 1` expose pour chaque symbole le
nom simple et qualifié, le paquet, le module et l’import requis, la nature, la
signature, les paramètres et leur type, le type de retour, les paramètres et
contraintes génériques disponibles, le résumé, la documentation, la
visibilité, le lien stable et les informations de dépréciation/remplacement.
La signature canonique conserve les modificateurs de contrat, notamment
`tailrec` (`borrow tailrec def`, `consume tailrec def` pour les méthodes), et
l'empreinte d'interface publique varie si ce modificateur change.
Les lecteurs refusent une version inconnue afin d’éviter une interprétation
silencieuse incompatible. Les
modules, types, variantes, traits, fonctions, globales et membres publics y
sont recensés ; les déclarations `private` et les membres `internal` sont
exclus. Les champs historiques sont conservés. Chaque entrée expose aussi
`summary`, `details`, `parameters`, `returns` et `examples`, dans un ordre
déterministe.

## Recherche hors ligne

`janus doc --search QUERY` interroge le même index public :

```bash
janus doc --search write
janus doc --search std.fs.write --format json
janus doc --search "atomic write" --module std.fs --kind function
janus doc --search random --package stdlib
```

Le format `human` (par défaut) affiche signature, import, paquet, résumé et
lien vers la documentation. Le format `json` est stable et scriptable. La
commande fonctionne aussi hors d’un projet : elle recherche alors dans l’index
installé de la bibliothèque standard. Depuis un projet, elle ajoute son index
et ceux des dépendances résolues. Le classement déterministe privilégie le nom
simple, le nom qualifié, la proximité lexicale, puis la signature et la
documentation ; côté LSP, il favorise aussi le module déjà importé et la
compatibilité du type de retour, de l’arité et de la généricité. Les égalités
sont départagées par nom qualifié, signature et paquet. La recherche charge en
priorité les `api-index.json` installés ou générés de la bibliothèque standard,
du projet et des dépendances, puis utilise les sources locales comme repli
lorsqu’un index manque. Aucun accès réseau n’est effectué.

Dans l’éditeur, les complétions globales publiques indiquent leur module et
ajoutent un `additionalTextEdits` pour l’import absent. Un import existant
n’est jamais dupliqué. En cas de collision du même nom entre modules, les
candidats restent visibles et explicites mais aucune édition d’import n’est
attachée. Les complétions après `.` conservent le chemin typé existant.

La directive documentaire `@deprecated use [[module.symbole]]` (le module peut
être omis) marque une API dans l'index, la documentation, la complétion et
produit le warning `JANA0033` à chaque appel. Les
contraintes génériques publiées correspondent aux contraintes représentables
par l’AST courant ; aucun mot-clé ou score d’usage non déterministe n’est
inventé.

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
