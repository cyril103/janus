# Performance du compilateur

Janus versionne deux projets canoniques sous `benchmarks/compilation/` :

- `small`, un programme autonome court ;
- `medium`, un projet multi-module avec structures, classe, enum et boucles.

Ils visent la détection de tendances, pas la comparaison entre machines.

## Rapport d'un build

`janus build --timings` mesure avec une horloge monotone :

| Phase | Contenu |
| --- | --- |
| `loading` | lecture des sources de l'entrée et de tous les imports |
| `parsing` | lexing et construction des AST de tous les modules |
| `analysis` | analyse sémantique |
| `llvm_generation` | génération et vérification de l'IR LLVM |
| `optimization` | pipeline cible LLVM, niveau d'optimisation et émission objet |
| `link` | édition de liens native |
| `overhead` | création/écriture/nettoyage d'artefacts et intervalles non attribués |

`total_ms` couvre la totalité de `build`, de la préparation de la sortie à la
fin du lien. La somme des phases est donc le total, aux seuls arrondis
millisecondes près. Avec `--emit`, les phases non exécutées valent zéro.

Le mode humain écrit sur stderr pour ne pas modifier les sorties ordinaires. Le
mode `--timings=json` écrit un document unique sur stdout :

```json
{
  "schema_version": 1,
  "command": "build",
  "unit": "milliseconds",
  "source": "src/main.janus",
  "total_ms": 42.0,
  "phases": {
    "loading": 1.0,
    "parsing": 2.0,
    "analysis": 3.0,
    "llvm_generation": 10.0,
    "optimization": 15.0,
    "link": 10.0,
    "overhead": 1.0
  }
}
```

## Coût et limites de la mesure

Quand les timings sont désactivés, Janus ne consulte pas l'horloge dans le
chargeur de modules et ne construit aucun rapport. Quand ils sont activés, le
coût ajouté correspond à deux lectures d'horloge par module, aux bornes des
autres phases et au rendu final. Les petits programmes sont donc les plus
sensibles au bruit de mesure. L'initialisation à froid du système, LLVM, Clang
et LLD reste volontairement incluse : elle fait partie du temps perçu par
l'utilisateur.

Un résultat ne doit être comparé qu'à des exécutions du même runner, de la même
configuration Release et du même corpus. Les chiffres ne sont pas une garantie
de performance contractuelle.

## Dashboard et alertes

Le workflow **Compiler performance dashboard** exécute chaque projet cinq fois
avec `scripts/benchmark-compilation.py`, publie le rapport brut, l'état de
tendance et un tableau dans le résumé GitHub Actions.

`scripts/compilation-trend.py` maintient une baseline par projet :

1. la médiane des cinq exécutions est comparée à la baseline ;
2. une hausse d'au moins 15 % devient un candidat ;
3. l'alerte n'est publiée qu'après deux jobs consécutifs au-dessus du seuil ;
4. une mesure sous le seuil remet le compteur à zéro et devient la nouvelle
   baseline.

L'alerte est informative (`warning`) : elle ne met jamais la CI en échec. Les
jobs sont sérialisés et reprennent uniquement l'état du dernier job réussi sur
la même branche. Un état antérieur manquant ou invalide après qu'un job a été
identifié arrête l'évaluation au lieu de réinitialiser silencieusement la
baseline. Le rapport et l'état sont conservés comme artefact afin qu'une
variation isolée ne bloque ni une PR ni une release.

Pour reproduire localement :

```bash
python3 scripts/benchmark-compilation.py \
  --janus "$PWD/build/janus" \
  --benchmarks benchmarks/compilation \
  --samples 5 \
  --output build/compiler-performance.json
```
