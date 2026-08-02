# Construction incrémentale

`janus build` met en cache les artefacts réussis. Dans un projet, le cache se
trouve dans `target/.janus-cache/v1`; pour un fichier source autonome, il se
trouve dans `.janus-cache/v1` à côté du fichier.

L'empreinte d'un artefact inclut la version de Janus, la cible LLVM, le profil
et les options de compilation qui modifient le code produit, le chemin normalisé
et le contenu du source d'entrée, ainsi que l'interface publique et
l'implémentation de chaque module importé. Les clés utilisent SHA-256 et une
identité canonique complète est vérifiée en plus de l'empreinte : une collision
ne peut donc pas rendre une entrée compatible.

Une seconde empreinte représente le consommateur. Elle inclut seulement
l'interface publique des dépendances et les détails privés qui modifient leur
ABI (par exemple la disposition des champs d'un type public) : modifier le
corps d'une fonction privée ne l'invalide pas, contrairement à une signature,
un type, un symbole public ou un changement de disposition. Les corps
génériques font partie de cette interface de compilation, car leurs
spécialisations sont produites dans les consommateurs. Les cycles
d'initialisation et de finalisation des globales sont séparés entre consommateur
et dépendances, ce qui permet de remplacer les initialisations privées sans
régénérer le consommateur. Le bitcode du consommateur est réutilisé lorsque
cette empreinte reste stable ; seule la closure des dépendances est régénérée
puis remplace ses anciennes définitions. L'empreinte complète de l'artefact
continue d'inclure toute l'implémentation afin que le
binaire final ne puisse jamais être périmé.

Les artefacts et métadonnées sont publiés par renommage atomique. Une écriture
interrompue reste un fichier temporaire qui n'est jamais considéré comme une
entrée. À la lecture, Janus vérifie l'identité et le contenu de l'artefact. Une
entrée incomplète ou corrompue est supprimée puis reconstruite normalement.
Les sources sont compilées depuis un instantané des contenus empreintés, puis
revérifiées avant publication pour refuser une modification concurrente. Ces
règles s'appliquent aussi aux builds `--offline`.

`janus build --deny-warnings` exécute l'analyse exhaustive du projet avant la
restauration éventuelle de l'artefact. Les avertissements restent donc visibles
et bloquants de façon identique après un hit ou un miss du cache.

Pour diagnostiquer un problème ou forcer une compilation indépendante du
cache :

```bash
janus build --no-cache
```

Cette option ne lit ni n'écrit le cache. Pour supprimer tous les produits de
construction du projet, cache compris :

```bash
janus clean
```

`janus clean` supprime uniquement le répertoire `target` du projet et réussit
également lorsqu'il est déjà absent.
