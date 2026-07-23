# Dépanner Project Euler sans masquer les crashs

Ce playbook s'applique au corpus de production des problèmes 1 à 20 décrit par
[`tests/fixtures/project-euler/production.txt`](../tests/fixtures/project-euler/production.txt).
Chaque entrée associe un source sous `production/` à son oracle sous `expected/` ;
voir aussi le [README du corpus](../tests/fixtures/project-euler/README.md).

Il n'existe pas de « mode compilateur sûr ». `janus check` valide l'analyse du
source, mais ne prouve pas sa sûreté à l'exécution. De même, `--release` demande
une construction optimisée : ce n'est pas une protection contre les erreurs
mémoire.

## Vérification rapide d'un problème

Depuis la racine du dépôt, remplacez le chemin de Janus par le chemin **absolu**
de l'exécutable réellement testé :

```sh
scripts/verify-janus-euler-suite.sh \
  --project tests/fixtures/project-euler \
  --config tests/fixtures/project-euler/production.txt \
  --janus /absolute/path/to/janus \
  --problem 1 \
  --timeout 5 \
  --artifacts-dir /tmp/janus-euler-problem-1
```

Le répertoire d'artefacts contient `latest`, `index.tsv`, le `report.json` de
chaque exécution et, sous `problems/1/`, les sorties standard et d'erreur des
étapes `check`, `build` et `run`. `latest` est un fichier texte contenant le nom
du dernier répertoire publié : ouvrez le `report.json` de ce répertoire, puis
les logs de la première étape en échec. Un nouveau répertoire d'exécution est
créé à chaque invocation.

## Isoler manuellement `check`, `build` et `run`

Le vérificateur enchaîne ces trois étapes et leur fait partager le budget du
problème. Pour savoir laquelle échoue, reproduisez-les séparément depuis la
racine du dépôt. N'utilisez pas `janus run` ici : cette commande compile puis
exécute et ne sépare donc pas les deux phases.

```sh
JANUS=/absolute/path/to/janus
SOURCE=tests/fixtures/project-euler/production/problem1.janus
WORK=$(mktemp -d "${TMPDIR:-/tmp}/janus-euler-problem-1.XXXXXX")
PROGRAM="$WORK/problem1"

# Chaque étape conserve ses deux flux et ne démarre que si la précédente réussit.
"$JANUS" check "$SOURCE" \
  > "$WORK/check.stdout" 2> "$WORK/check.stderr"
CHECK_STATUS=$?
printf 'check exit=%s\n' "$CHECK_STATUS"

if [ "$CHECK_STATUS" -eq 0 ]; then
  "$JANUS" build "$SOURCE" -o "$PROGRAM" \
    > "$WORK/build.stdout" 2> "$WORK/build.stderr"
  BUILD_STATUS=$?
  printf 'build exit=%s\n' "$BUILD_STATUS"

  if [ "$BUILD_STATUS" -eq 0 ]; then
    "$PROGRAM" > "$WORK/run.stdout" 2> "$WORK/run.stderr"
    RUN_STATUS=$?
    printf 'run exit=%s\n' "$RUN_STATUS"
    if [ "$RUN_STATUS" -eq 0 ]; then
      diff -u tests/fixtures/project-euler/expected/problem1.txt \
        "$WORK/run.stdout"
    fi
  fi
fi
```

Conservez le code de sortie et les deux flux de chaque commande. Une réussite de
`check` suivie d'un échec de `build` localise le problème dans la génération ou
l'édition de liens ; une construction réussie suivie d'un échec de l'exécutable
localise le problème à l'exécution. Un code de sortie `0` avec un `diff` non
vide est une erreur de résultat, pas un crash.

Cette reproduction manuelle n'impose pas de délai maximal et ne supervise pas
l'arbre des processus. Pour classifier un blocage ou conserver des artefacts
comparables, le vérificateur reste la référence.

Le vérificateur compare quant à lui les sorties après suppression des espaces de
fin de ligne. Le `diff` exact ci-dessus est donc volontairement plus strict ; en
cas de différence purement liée aux espaces finaux, le champ
`output_mismatch` du rapport du vérificateur fait foi pour le corpus.

## Lire le rapport du vérificateur

Pour chaque résultat, utilisez ensemble `status`, `stage`, `exit_code`, les
booléens et les informations de terminaison :

- `ok` : analyse, construction, exécution et comparaison ont réussi ;
- `mismatch` à l'étape `run` : le programme est sorti avec le code `0`, mais sa
  sortie normalisée diffère de l'oracle (`output_mismatch: true`) ;
- `error` à `check` ou `build` : le compilateur a échoué sans signal observé ;
- `crash` : un processus a échoué à l'exécution ou a reçu un signal autre que
  `SIGSEGV` ;
- `segfault` : l'observateur a effectivement vu une terminaison par `SIGSEGV`
  (`termination: "signal"`, `signal_name: "SIGSEGV"`) ;
- `timeout` : le harnais a épuisé le budget à l'étape indiquée ou le budget
  global (`timeout: true`) ;
- `fallback` : la tentative primaire a échoué et la source de repli a réussi.

Le champ `stage` peut notamment valoir `source`, `expected`, `check`, `build`,
`run` ou `global`. Consultez toujours `message`, `stdout_log`, `stderr_log`,
`source` et `executable` lorsqu'ils sont présents.

**Un code 139 ne prouve pas à lui seul un segfault** : un programme peut sortir
intentionnellement avec 139. Exigez la terminaison observée par signal et
`SIGSEGV`. **Un code 124 ne prouve pas à lui seul un timeout** : le vérificateur
ne pose `timeout: true` que lorsque son propre harnais a déclenché l'expiration ;
un processus qui retourne lui-même 124 reste un échec de processus.

## Arbre de décision anti-segfault

1. **`stage` vaut `source` ou `expected` ?** Corriger le chemin dans le manifeste
   avant d'analyser le programme.
2. **Échec à `check` ?** Lire `check.stderr`, corriger syntaxe, types et imports,
   puis relancer le même problème. Ne pas conclure sur la sûreté d'exécution.
3. **Échec à `build` ?** Lire `build.stderr`, vérifier l'exécutable Janus et
   l'édition de liens ; ne pas modifier l'algorithme avant d'avoir reproduit
   cette étape seule.
4. **`mismatch` ?** Comparer la sortie à l'oracle et corriger le calcul ou le
   format, sans classer le cas comme crash.
5. **`timeout` réellement marqué ?** Borner les boucles, réduire les calculs
   répétés et vérifier les progressions à forte croissance. Relancer avec le
   même budget avant de l'augmenter.
6. **`segfault` confirmé par `SIGSEGV` ?** Regarder d'abord `stage`. À `check` ou
   `build`, reproduire la commande Janus correspondante, conserver ses logs et
   signaler un défaut du compilateur : l'exécutable peut ne pas exister. À
   `run`, reproduire l'exécutable isolé, puis auditer en priorité indices/bornes,
   pointeurs et durée de vie des objets.
7. **Autre `crash` ?** Partir du signal ou du code réellement rapporté, puis
   ouvrir le fichier `stderr_log` de l'étape indiquée ; ne pas le renommer en
   segfault.

Points numériques et mémoire à contrôler, de façon ciblée :

- `int` est signé sur 32 bits et `usize` non signé sur 64 bits ; choisir le type
  d'après la plage et éviter les casts implicites supposés ;
- `+`, `-` et `*` sur les entiers s'enroulent modulo leur largeur : réécrire par
  exemple une borne `d * d <= n` en `d <= n / d` lorsque les préconditions de
  division sont assurées ;
- valider les bornes avant tout accès par indice et distinguer longueur,
  capacité et indice ;
- Janus utilise une gestion mémoire manuelle : établir un propriétaire unique
  pour chaque objet créé avec `new`, préférer `defer delete`, et éviter fuite,
  double libération et accès après libération.

Les règles exactes sont détaillées dans le [guide du langage](language-guide.md),
notamment les entiers, collections, pointeurs et la gestion manuelle de la
mémoire. Les commandes et le warning non bloquant
`--warn-high-growth-loops` sont décrits dans la [documentation des
outils](tooling.md).

## Échelle de replis, dans l'ordre

Ne sautez à l'échelon suivant qu'après avoir conservé la reproduction et les
artefacts de l'échelon courant :

1. **Même source, entrée minimale pertinente** : réduire localement la borne ou
   le jeu de données pour identifier la première valeur fautive, puis restaurer
   l'entrée canonique.
2. **Même algorithme, invariants explicites** : ajouter gardes de bornes et de
   division, choisir `int`/`usize` selon la plage, borner les boucles et clarifier
   la propriété/libération des allocations.
3. **Même résultat, calcul intermédiaire moins risqué** : diviser avant de
   multiplier lorsque l'identité le permet, utiliser `d <= n / d`, ou remplacer
   une structure indexée par une itération bornée.
4. **Algorithme alternatif** : créer une autre source qui calcule toujours la
   réponse dynamiquement et la tester contre le même oracle de production.
5. **Repli automatisé explicite** : fournir cette source alternative dans un
   manifeste séparé avec `--safe-fallback`; garder l'échec primaire visible dans
   le JSON et ouvrir un ticket. Le repli n'est pas une correction du primaire.

### Sémantique exacte de `--safe-fallback`

Le dépôt ne livre pas de manifeste de repli. Les chemins suivants sont donc des
**exemples à créer**, pas des fichiers existants :

```text
# path/to/fallback.txt — chemins relatifs au projet Project Euler
1|path/to/problem1-fallback.janus|expected/problem1.txt
```

```sh
scripts/verify-janus-euler-suite.sh \
  --project tests/fixtures/project-euler \
  --config tests/fixtures/project-euler/production.txt \
  --safe-fallback path/to/fallback.txt \
  --janus /absolute/path/to/janus \
  --problem 1 \
  --timeout 5 \
  --artifacts-dir /tmp/janus-euler-problem-1-fallback
```

`--safe-fallback FILE` charge une **configuration/source alternative** pour
chaque problème sélectionné. Le primaire est essayé d'abord ; le repli n'est
lancé que si le primaire échoue. Les deux tentatives partagent un unique budget
`--timeout` par problème, et le repli ne reçoit que le temps restant. Le budget
`--global-timeout`, s'il est fourni, reste partagé par toute l'invocation.

Le vérificateur exige aussi que l'oracle du repli, après la même normalisation
des espaces finaux, soit identique à l'oracle primaire. Il conserve les deux
tentatives dans le rapport (`primary`, `fallback`, `fallback_used`). En mode
repli, l'analyse primaire active également le warning statique non bloquant
`--warn-high-growth-loops`; cela ne transforme ni le binaire ni son exécution.
Malgré son nom, `--safe-fallback` n'est donc ni une instrumentation, ni un mode
compilateur garantissant la sûreté : c'est l'orchestration d'une seconde source
contre le même oracle et dans le même budget.

## Vérifier toute la suite

La commande correspondant aux budgets du corpus de production est :

```sh
scripts/verify-janus-euler-suite.sh \
  --project tests/fixtures/project-euler \
  --config tests/fixtures/project-euler/production.txt \
  --janus /absolute/path/to/janus \
  --all \
  --timeout 5 \
  --global-timeout 120 \
  --artifacts-dir /tmp/janus-euler-artifacts
```

`--timeout 5` est le budget partagé par `check`, `build` et `run` pour chaque
problème ; `--global-timeout 120` borne l'ensemble de l'invocation.

## Checklist d'un rapport d'incident

Joindre ou indiquer :

- le numéro du problème et la ligne correspondante de `production.txt` ;
- le commit, le système/architecture, `janus --version` et le chemin absolu de
  l'exécutable Janus utilisé ;
- la commande exacte, notamment `--timeout`, `--global-timeout`, `--release` et
  `--safe-fallback` s'ils étaient présents ;
- le code de sortie du vérificateur et son `report.json` ;
- `status`, `stage`, `termination`, `signal_name`, `signal_number`,
  `conventional_exit_code` et les booléens `segfault`, `timeout`,
  `output_mismatch` ;
- les logs `check`/`build`/`run` des artefacts et tout `stack_excerpt` présent ;
- le source minimal reproductible, l'entrée ou la borne déclenchante, la sortie
  attendue et la sortie obtenue ;
- pour un repli, les deux entrées `primary`/`fallback`, le manifeste alternatif
  et la confirmation que l'oracle est inchangé ;
- les modifications déjà essayées sur les types, bornes, boucles, allocations
  et libérations.
