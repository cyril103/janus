# Changelog

Les changements notables de Janus sont documentés dans ce fichier. Le projet
utilise le versionnage sémantique à partir de sa première version publique.

## [Non publié]

## [0.17.0] - 2026-08-20

Cette version mineure permet aux applications graphiques de contrôler si leur
fenêtre peut être redimensionnée par l'utilisateur.

### Bibliothèque graphique

- ajout de `std.graphics.drawing.setWindowResizable`, relayé dynamiquement
  vers raylib sur Linux, macOS et Windows.

## [0.16.0] - 2026-08-20

Cette version mineure permet aux applications graphiques d'échanger du texte
UTF-8 avec le presse-papiers du système.

### Bibliothèque graphique

- ajout de `std.graphics.input.setClipboardText` et `clipboardText`, relayés
  dynamiquement vers raylib sur Linux, macOS et Windows.

## [0.15.0] - 2026-08-20

Cette version mineure ajoute des entrées-sorties non bloquantes pour les
processus enfants et renforce la sûreté des emprunts conservés.

### Processus et système

- ajout de `ChildProcess.tryRead` et `ChildProcess.tryWrite`, avec progression
  partielle et résultat `WouldBlock`, sur Linux, macOS et Windows ;
- conservation des contrats bloquants historiques de `read` et `write`,
  désormais construits sur les primitives portables de sondage.

### Langage et sûreté

- suivi des relations entre propriétaires et emprunteurs pour les alias locaux
  `borrow val` et les champs constructeur `borrow`, y compris après `move` ;
- refus de `delete` et `free` tant qu'un emprunteur vivant référence encore le
  propriétaire, afin d'empêcher les lectures après libération.

## [0.14.0] - 2026-08-17

Cette version mineure permet aux interfaces graphiques natives d'adapter le
pointeur de souris au contexte d'interaction.

### Bibliothèque graphique

- ajout de l'enum public `MouseCursor`, qui expose les formes système usuelles,
  notamment les pointeurs de redimensionnement horizontal et vertical ;
- ajout de `std.graphics.input.setMouseCursor`, relayé dynamiquement vers
  raylib sur Linux, macOS et Windows.

## [0.13.0] - 2026-08-17

Cette version mineure fournit les primitives nécessaires aux éditeurs natifs :
saisie Unicode fidèle, serveur de langage persistant et processus enfants
interactifs portables.

### Bibliothèque graphique

- ajout de `std.graphics.input.characterPressed`, qui retourne les caractères
  Unicode selon la disposition active du clavier et permet notamment une saisie
  correcte avec les claviers AZERTY ;
- ajout de `std.graphics.input.disableExitKey`, qui permet aux interfaces
  modales d'utiliser `Escape` sans déclencher la fermeture de la fenêtre.

### Processus et système

- ajout de `std.process.currentWorkingDirectory` et du propriétaire
  `WorkingDirectory` pour capturer le répertoire courant sans tampon fixe ;
- ajout de `spawnProcess` et de `ChildProcess`, avec écriture sur l'entrée
  standard, lecture incrémentale des sorties, fermeture explicite de l'entrée
  et destruction sûre sur Linux, macOS et Windows.

### Serveur de langage

- prise en charge d'une session JSON-RPC persistante dans `janus-lsp`, avec
  cycle `initialize`, synchronisation `didOpen`/`didChange`/`didClose` et
  plusieurs requêtes successives sur le même processus ;
- enrichissement de la complétion typée pour les symboles du workspace, les
  membres d'`Array` et les classes déclarées dans le fichier courant.

## [0.12.0] - 2026-08-14

Cette version mineure étend les opérations de fichiers et de tests, formalise
la plage LLVM prise en charge et sécurise la remise de l’extension VS Code.

### Bibliothèque standard et tests

- ajout de `std.fs.removeDirectoryAll`, suppression récursive idempotente qui
  ne suit pas les liens symboliques, et de sa prise en charge native portable ;
- ajout de `TestTemporaryDirectory.cleanup()` dans `std.testing`, avec
  nettoyage récursif observable et nettoyage best-effort par le destructeur.

### Toolchain et distribution

- validation explicite de la plage LLVM supportée, de LLVM 18 à LLVM 21, dans
  CMake et dans une matrice CI dédiée ;
- production, test, inventaire et checksum du VSIX par la CI, puis remise au
  mainteneur pour upload manuel : aucune publication Marketplace automatique
  ni aucun secret Marketplace ne sont utilisés par le dépôt.

## [0.11.1] - 2026-08-13

Cette version corrective renforce les garanties de publication introduites en
0.11.0 après une revue indépendante tardive du candidat.

### Fiabilité et sécurité de release

- le digest des worktrees sales couvre désormais sans ambiguïté les fichiers
  non suivis dont le chemin contient espaces, Unicode, guillemets ou antislashs ;
- le canari refuse les chemins Windows absolus, racines multiples, conflits
  fichier/répertoire, types spéciaux et archives excessives avant extraction ;
- l’identité empaquetée est validée complètement puis comparée à celle du
  binaire extrait ; les répertoires temporaires, cache, registre et XDG sont
  isolés ;
- la publication attend explicitement l’artefact VS Code et sélectionne
  l’archive Linux candidate par son nom exact.

## [0.11.0] - 2026-08-13

Cette version mineure enrichit les entiers, le pattern matching et les tableaux,
et rend les toolchains candidates identifiables et vérifiables de bout en bout.

### Langage et bibliothèque standard

- ajout des littéraux entiers binaires, octaux et hexadécimaux, ainsi que des
  opérateurs bit à bit et de décalage avec sémantique portable et diagnostics
  pour les comptes invalides ;
- ajout des patterns littéraux, des gardes de `match` et des contrôles de
  propriété, de portée et d’homonymie associés ;
- ajout des littéraux de tableaux typés et des fabriques `Array`, avec gestion
  sûre des paniques, débordements de capacité et nettoyages imbriqués ;
- ajout d’un tri hybride stable pour `Array` et transmission des arguments à
  l’exécutable lancé par `janus run`.

### Toolchain, distribution et CI

- ajout d’une identité cohérente pour `janus`, `janus-lsp`, `janusup`, les
  archives et le cache : version, SHA, canal, état dirty, cible et LLVM sont
  disponibles via `--version --json` et dans les métadonnées packagées ;
- ajout d’un canari Janus8 épinglé qui consomme exclusivement l’archive Linux
  candidate et bloque release comme nightly avant publication ;
- création du canal nightly multi-plateforme atomique, avec version immuable,
  checksums, attestations, validation par `janusup` et promotion tardive ;
- sérialisation et staging transactionnel des sorties, lockfiles et écritures
  de cache concurrentes, y compris le mode `--no-cache` et Windows ;
- alignement des outils runtime et des noms d’archives Linux, macOS et Windows.

### Outils et qualité

- correction de la reconnaissance des littéraux flottants complets par
  l’extension VS Code ;
- conservation de l’AddressSanitizer sur macOS tout en neutralisant uniquement
  le bruit connu de détection des fuites système.

## [0.10.0] - 2026-08-11

Cette version mineure étend le langage avec des imports précis, l’inférence
locale et l’évaluation à la compilation, ajoute un framework de tests natif et
renforce les frontières de sécurité de la toolchain. Janus reste pré-1.0.

### Langage et diagnostics

- ajout des imports qualifiés, sélectifs et renommés, avec visibilité confinée
  au module importeur et résolution canonique dans les graphes en diamant ;
- extension de l’inférence de type aux variables locales, avec diagnostics
  ciblés et conservation des contrats de propriété ;
- ajout de `const`, `const def` et `staticAssert`, avec évaluation déterministe
  à la compilation, contrôle de pureté, budgets de ressources et prise en
  charge des dépendances inter-modules ;
- ajout de `checkedCast`, `saturatingCast` et `truncatingCast`, du suffixe de
  littéral `float` (`0.5f`) et de diagnostics orientant vers la politique de
  conversion adaptée ;
- définition portable des bornes, signes, fractions, valeurs non finies et
  pertes de précision, identique entre repliement constant et backend LLVM.

### Outils et serveur de langage

- ajout d’un index partagé de découverte des API, consommé par la CLI, la
  documentation et le serveur de langage ;
- correction de la navigation et de l’aide à la signature pour les alias,
  imports et documents fermés, avec plages ancrées dans le document demandé ;
- intégration des constantes à la documentation générée, au LSP, au formateur
  et au cache incrémental.

### Bibliothèque standard

- ajout de `std.numeric` et de l’enum public `NumericCastError` ;
- ajout de `std.testing` pour les assertions des tests natifs.

### Tests

- ajout des fonctions `/// @test`, de `std.testing` et de l’isolation par
  processus avec paniques attendues, tests ignorés ou série ;
- ajout du filtrage exact, du parallélisme, des timeouts, de la politique de
  suite vide et des rapports humains, JSON versionné et JUnit ;
- suppression des avertissements de sûreté dans les 36 exemples Janus et ajout
  d’une gate CI qui refuse toute nouvelle régression.

### Sécurité et fiabilité

- validation défensive des archives de toolchain avant extraction dans
  `janusup`, les installateurs POSIX et PowerShell : confinement de la racine,
  refus des liens, types spéciaux et noms ambigus, détection des collisions et
  limites de décompression ;
- exigence d’attestations officielles pour les distributions et
  transactionnalisation des installations concurrentes de `janusup` ;
- sérialisation des remplacements du cache de registre et durcissement des
  assertions de tests en configuration Release ;
- épinglage immuable des GitHub Actions utilisées par le dépôt.

### Documentation

- publication de la matrice exhaustive des conversions numériques explicites ;
- mise à jour du Book, de la référence et du site pour les imports, les tests,
  l’inférence locale et les constantes à la compilation.

## [0.9.0] - 2026-08-02

Cette version mineure renforce l'analyse de sûreté et les contrats de
propriété à la frontière C, tout en élargissant les surfaces mathématique et
graphique de la bibliothèque standard.

### Langage et diagnostics

- ajout des avertissements `JANA0002` à `JANA0022` pour les fuites, les
  écrasements de propriétaires, les nettoyages incomplets, les emprunts
  temporaires, les cycles potentiels et les conversions numériques à risque ;
- ajout des contrats `borrow` et `consume` sur les paramètres pointeur des
  fonctions externes, et `borrow` ou `owned` sur leurs retours ;
- ajout des emprunts locaux et des champs observants, ainsi que des primitives
  explicites `numericCast`, `owningCapture`, `freeStorage`,
  `reallocPreserving` et `adoptReallocation` pour les opérations bas niveau ;
- ajout de l'analyse optionnelle `--warn-high-growth-loops` et d'une couverture
  de régression dédiée aux diagnostics de propriété.

### Graphisme 2D

- extension de la façade raylib aux images CPU, formats de pixels, primitives
  géométriques, splines, collisions, ciseaux, modes de fusion et textures
  avancées ;
- conservation des contrats de ressources propriétaires et couverture du
  runtime avec un backend raylib factice élargi.

### Bibliothèque standard

- extension de `std.math` avec le noyau scalaire C99 en `double` et `float`,
  des constantes usuelles, la classification IEEE, `clamp` et `lerp` ;
- ajout d’une ABI mathématique propre au runtime et du lien automatique avec
  `libm` sur les plateformes Unix qui la séparent de la libc.

### Documentation

- synchronisation du site et du Book avec `borrow`, les contrats externes
  `consume`/`owned`, `numericCast`, les avertissements de sûreté et la nouvelle
  surface de `std.math` ;
- publication de la référence structurée des diagnostics dans le site.

### Correctifs

- correction de la validation des versions de release et de plusieurs cas
  limites du runtime ;
- suppression des avertissements numériques et de propriété dans la
  bibliothèque standard.

## [0.8.1] - 2026-08-01

Cette version corrective stabilise l'affichage des booléens à la frontière
entre le code LLVM généré et le runtime C.

### Backend LLVM et runtime

- alignement de l'ABI de `janus_print_bool` sur une représentation 8 bits
  explicite et extension des valeurs LLVM `i1` avant `debug`, `print` et
  `println` ;
- ajout d'une régression couvrant une struct contenant un `bool`, retournée par
  une fonction puis affichée dans une boucle `for`.

### Documentation

- finalisation de la référence francophone avec des mini-exemples d'utilisation
  pour les API publiques.

## [0.8.0] - 2026-07-31

Cette version cumulative publie les travaux menés depuis 0.7.6 sur les
performances, les builds incrémentaux et le registre, puis audite et gèle un
candidat de surface publique sans activer les garanties définitives de 1.0.

### Audit et gel pré-1.0

- publication d'un inventaire versionné classant toutes les surfaces publiques
  en `stable-candidate` ou `experimental`, avec contrôle automatique des
  signatures stdlib, de la CLI et des liens ;
- publication de la migration 0.5 vers 0.8, des limites connues, de la politique
  de sévérité et d'un rapport NO-GO explicite pour 1.0 ;
- extension de la compatibilité N/N+1 au binaire 0.7.6 publié et au candidat ;
- ajout de corpus versionnés lexer, parseur, manifeste et résolution, chacun
  exercé par une campagne ASan/UBSan planifiée de 60 minutes.

### Client de registre distant

- ajout de `janus search`, de registres par défaut ou explicites et de la
  publication distante authentifiée selon le protocole v1 ;
- résolution et téléchargement avec identité complète, métadonnées, manifeste,
  tailles et SHA-256 vérifiés avant extraction ;
- cache publié atomiquement, réutilisable avec `--locked --offline`, sans
  jeton dans les sorties, diagnostics ou lockfiles ;
- maintien du registre local historique pour compatibilité et tests.

### Registre de référence

- ajout d'un service v1 conteneurisé sans dépendance d'exécution externe, avec
  blobs immuables, index SQLite transactionnel et autorités par namespace ;
- reçus de provenance HMAC-SHA-256 et journal d'audit signé et chaîné pour les
  publications, refus et changements de yank ;
- sauvegarde cohérente, restauration vérifiée dans une nouvelle racine et
  procédures de déploiement, administration et réponse à incident ;
- test d'interopérabilité couvrant publication, installation, refus d'accès et
  de remplacement, yank, sauvegarde/restauration et `--locked --offline`.

### Protocole du registre

- publication du protocole distant `v1`, de ses six schémas JSON stricts et
  de fixtures de référence pour l'index, les métadonnées, les archives et la
  résolution verrouillée ;
- contrat d'immuabilité des versions, yanking compatible avec les lockfiles,
  négociation explicite et identité complète `(registre, namespace, paquet)` ;
- modèle de sécurité couvrant authentification, autorisation, confusion de
  dépendance, traversée de chemin, sauvegarde et récupération.

### Construction incrémentale

- ajout d'un cache d'artefacts indexé par la version Janus, la cible, les
  options, le source et les interfaces publiques des dépendances ;
- séparation de l'empreinte consommateur et de l'empreinte d'artefact afin
  qu'un changement d'implémentation privée sans effet ABI n'invalide pas le
  consommateur, tandis qu'un changement public ou de disposition l'invalide et
  que le binaire final reste à jour ;
- écritures atomiques compatibles avec les builds concurrents, validation de
  l'identité et du contenu à la lecture, et reconstruction automatique après
  interruption, corruption ou collision ;
- ajout de `janus build --no-cache` et de la commande idempotente
  `janus clean`.

### Performances du compilateur

- ajout de `janus build --timings` et `--timings=json`, avec attribution du
  temps total au chargement, parsing, analyse, génération LLVM,
  optimisation/émission objet, lien et surcoût résiduel ;
- ajout de projets de benchmark petits et moyens, mesurés sur cinq exécutions ;
- publication d'un dashboard GitHub Actions non bloquant, dont l'alerte exige
  une hausse de médiane d'au moins 15 % confirmée par deux jobs consécutifs.

## [0.7.6] - 2026-07-29

Cette version cumulative publie les fonctions de navigation LSP de la 0.7.5
avec l'extension VS Code enrichie et sa chaîne de distribution reproductible.

### Serveur de langage

- ajout du renommage sémantique atomique à l'échelle du workspace, de l'aide à
  la signature, des jetons sémantiques, des types déduits configurables et de
  la navigation vers les implémentations de traits ;
- annonce des capacités LSP correspondantes et conservation des frontières de
  module, de portée et de visibilité lors du renommage.
- ajout de quick fixes fondés sur des `WorkspaceEdit` pour les suggestions
  structurées, les imports manquants non ambigus et les branches `match`
  manquantes, sans application automatique des choix à vérifier ;
- transmission des notes, emplacements secondaires et corrections structurées
  avec les diagnostics publiés.

### Extension VS Code

- tests du choix de `janus-lsp`, matrice extension/toolchain et procédures
  d'installation et de mise à jour ;
- publication Marketplace reproductible à partir du même VSIX construit sur
  le tag de release.

## [0.7.4-hotfix.1] - 2026-07-28

### Backend LLVM

- allocation unique dans le bloc d’entrée des variables locales et temporaires
  de taille fixe, afin que les boucles `while` et `for` conservent une
  consommation de pile bornée quel que soit leur nombre d’itérations ;
- couverture des déclarations `val`/`var`, bindings d’itérateur et de `match`,
  temporaires de structs et paramètres transitoires de constructeurs.

## [0.7.4] - 2026-07-28

Cette version cumulative rassemble les gates validées depuis 0.6.0, qui
n’avaient pas été publiées séparément, puis modernise et documente toute la
bibliothèque standard avant les prochains lots d’outillage.

### Documentation de la bibliothèque standard

- documentation `///` des 28 modules et des 637 symboles publics de la
  bibliothèque standard, y compris types, variantes, traits et membres ;
- ajout de `janus doc --stdlib`, avec génération hors ligne déterministe,
  couverture stricte et rejet des liens non résolus ;
- publication de l’index HTML/JSON sur le site et dans les archives, avec
  comparaison octet par octet lors des builds ;
- ajout d’un doctest de succès par module et de `compile_fail` pour les
  familles d’absence et d’erreur structurée.

### Graphisme et audio

- dérivation explicite de `Copy`, `Equality` et `Debug` pour les enums,
  vecteurs, rectangles et couleurs graphiques structurels ;
- unification des chargements invalides autour d'un handle nul et conservation
  des destructeurs propriétaires pour textures, fontes, render targets,
  shaders, sons et musiques ;
- suivi natif des portées drawing, caméra, render target et shader : les fins
  sans ouverture et les ouvertures répétées sont ignorées, tandis que
  `closeWindow` termine les portées encore actives ;
- ajout d'une fixture Janus ASan/UBSan avec backend raylib factice couvrant
  chargements invalides, déplacement, sortie anticipée, destruction exacte et
  déséquilibres `begin`/`end`.

### Système, texte, flux et ressources

- unification de l'état ouvert/fermé de `SystemFile`, `DirectoryIterator`,
  `InputStream` et `OutputStream` autour du handle natif, sans booléen
  redondant et avec fermeture exactement une fois ;
- conservation systématique de l'opération, de la catégorie, du code natif et
  du contexte dans les erreurs de fichiers, flux et processus, y compris après
  fermeture ;
- factorisation des erreurs d'allocation de processus et des conversions
  vérifiées de durées, sans modification de la surface publique ;
- renforcement des fixtures ASan/UBSan pour les erreurs natives, fermetures
  répétées, entrées/sorties partielles et chemins Unicode, avec benchmark
  versionné des services de la bibliothèque standard.

### Collections hachées et dérivations

- sondage des doublons et remplacements avant toute croissance afin d'éviter
  les rehashs sans nouvelle entrée ;
- compaction à capacité constante lorsque les tombstones, plutôt que les
  entrées vivantes, franchissent le seuil de charge de 75 % ;
- calcul du seuil partagé sans multiplication susceptible de déborder et
  égalité primitive alignée sur la dérivation structurelle ;
- ajout d'une suite commune map/set, de chemins propriétaires sous ASan,
  d'un test de panique et d'un benchmark versionné.

### Cœur fonctionnel et séquences

- passage de `Array.intoIterator()` à un parcours linéaire qui transfère les
  éléments dans l'ordre et détruit exactement une fois la partie non visitée ;
- remplacement des récursions de rejet de `Iterator.filter` et de saut des
  sous-itérateurs vides de `flatMap` par des boucles à pile bornée ;
- conservation du contrat d'emprunt des callbacks synchrones et du nettoyage
  propriétaire des closures stockées par les adaptateurs d'itérateur ;
- ajout de stress tests ASan/UBSan pour valeurs `Copy` et propriétaires, et
  d'un benchmark versionné de pipeline.

### Doctests et exemples compilés

- ajout des directives `// doctest: doctest`, `incomplete` et
  `compile_fail=CODE`, indépendantes du texte des diagnostics ;
- intégration des exemples Markdown à `janus test`, avec filtre commun,
  contexte du paquet, chemins `--doc-path` et mode `--doc` ;
- migration des extraits autonomes du site vers le même moteur, avec
  signalement des échecs par document et ligne.

### Documentation d’API

- ajout des commentaires `///` pour les modules, types, variantes, traits,
  fonctions, globales et membres, conservés dans l’AST ;
- ajout de `janus doc`, de `janus doc --open` et d’une sortie HTML autonome
  accompagnée d’un index public JSON déterministe ;
- résolution des liens `[[Symbole]]`, avertissements pour les références
  absentes ou ambiguës, et exclusion par défaut des éléments privés ou
  internes.

### Flux, processus et bibliothèque standard

- ajout de `std.io` avec buffers d’octets propriétaires, entrées et sorties
  séquentielles tamponnées, EOF, flush et fermeture définie ;
- ajout des wrappers non propriétaires pour les trois flux standards et de
  `copyStream`, qui gère les lectures et écritures partielles ;
- décodage UTF-8 explicite des buffers binaires et exemples exécutés de copie
  de fichier et de traitement ligne par ligne.
- ajout de `std.process` pour les arguments du programme, les variables
  d’environnement et le lancement synchrone sans shell ;
- ajout des codes de sortie, captures binaires stdout/stderr et répertoires de
  travail, avec conservation des arguments contenant espaces ou Unicode.

### Runtime portable, chemins et fichiers

- ajout de `SystemError`, de catégories portables conservant le code natif et
  d’un contexte d’opération transportable dans `Result` ;
- ajout de `SystemFile`, `openSystemFile` et `removeSystemFile`, avec chemins
  UTF-8 stricts, lecture/écriture en octets et fermeture native exactement une
  fois ;
- isolation des appels POSIX et Windows dans le runtime et couverture de leur
  contrat dans la matrice CI Linux, macOS et Windows.
- ajout de `std.path` avec chemins UTF-8 propriétaires, jointure,
  normalisation lexicale et composants selon les séparateurs natifs ;
- ajout de `std.fs` avec lecture propriétaire, écriture atomique, répertoires
  temporaires, parcours, suppression et métadonnées sans suivi du dernier
  lien.

### Dérivations et outillage

- réservation de la clause explicite `derives` pour `Copy`, `Equality`,
  `Hashing` et `Debug` sur les structs, enums et classes pertinents, sans
  système général de macros ;
- représentation typée des demandes dans l'AST, diagnostics des capacités
  inconnues ou répétées, préservation par le formatter et complétion LSP ;
- spécification normative de l'éligibilité champ par champ, des génériques,
  de la visibilité, des diagnostics et des contraintes de propriété avant la
  génération des opérations.
- génération structurelle de `Copy`, `Equality`, `Hashing` et `Debug`, avec
  refus des agrégats propriétaires pour `Copy` et diagnostics localisant le
  premier champ ou payload incompatible ;
- ajout de `DerivedHashing[T]` dans `std.hashing` pour employer directement
  les clés utilisateur dérivées dans `HashSet` et `HashMap`, et de
  `debug(value)` pour une représentation diagnostique déterministe.

### `Option`, `Result` et propagation

- ajout des combinateurs `isSome`, `isNone`, `map`, `andThen`, `orElse` et
  `unwrapOr` dans `std.option`, avec observation bornée et variantes
  consommantes pour les valeurs propriétaires ;
- destruction exacte des valeurs de repli et closures inutilisées, sans
  extraction ni copie implicite d'une ressource.
- ajout de `isOk`, `isError`, `map`, `mapError`, `andThen`, `orElse`,
  `unwrapOr`, `toOption` et `fromOption` dans `std.result`, avec transfert
  exact de la variante active et conversions vers `std.option` ;
- propagation `?` explicite des `Result` propriétaires via `(move value)?`,
  sans réutilisation possible après transfert et avec exécution des
  nettoyages actifs.

### Collections et itérateurs propriétaires

- ajout des parcours consommateurs `intoIterator()` pour `Array` et `HashSet`
  et `intoEntries()` pour `HashMap`, avec transfert explicite des valeurs
  propriétaires et destruction des éléments non visités ;
- adaptation des pipelines `map`, `filter`, `flatMap`, `take`, `fold` et des
  builders aux éléments propriétaires, sans copie implicite ;
- nettoyage des itérateurs lors des sorties `break`, `continue`, `return`, `?`
  et des paniques, et diagnostics des tentatives d'échappement depuis une
  observation bornée.

## [0.6.0] - 2026-07-27

Cette version établit le contrat de propriété des conteneurs et permet aux
collections principales de stocker, déplacer et détruire correctement des
valeurs propriétaires.

### Langage et bibliothèque standard

- définition du contrat de propriété des conteneurs : observation bornée,
  transferts explicites, invalidation, réallocation et nettoyage des valeurs
  propriétaires ;
- prise en charge des classes, structs et enums propriétaires dans `Array[T]`,
  avec transferts explicites, destruction exacte lors de `set`, `clear` et de
  la destruction du tableau, et nouvelles opérations `replace` et `remove`.
- extension de `HashSet`, `HashMap` et des builders aux éléments propriétaires,
  avec rehash par déplacement, tombstones non propriétaires, remplacement et
  suppression sans double destruction, et observation bornée pour le hachage.

## [0.5.2] - Non publiée

### Diagnostics

- ajout des rendus terminal avec extraits et repères, et JSON conforme à un
  schéma 0.5.2 versionné pour `janus check` et `janus build` ;
- représentation structurée des suggestions sans modification automatique des
  sources ;
- récupération du parseur entre déclarations indépendantes afin de publier
  plusieurs diagnostics sans cascade.

## [0.5.1] - Non publiée

### Documentation

- ajout d'un inventaire JSON versionné de la surface publique 0.5.x couvrant
  les modules et symboles de la bibliothèque standard ainsi que les commandes
  et options affichées par `janus --help` ;
- classement explicite des API candidates à la stabilité, des API
  expérimentales et du module d'implémentation `std.hash_probe` ;
- publication de l'inventaire avec la référence du site et vérification des
  exemples Janus autonomes et des liens internes ;
- détection automatique des dérives de symboles, signatures et options CLI,
  avec fixture obsolète ciblée et exécution des extraits et du crawler dans la
  CI Pages.

## [0.5.0] - 2026-07-25

Cette version enrichit la bibliothèque standard et le graphisme, étend
l'analyse des projets complets dans les outils et prépare les garanties de
compatibilité attendues pour Janus 1.0.

### Langage et bibliothèque standard

- ajout des chaînes `else if` au parseur et à l'analyse sémantique ;
- ajout des modules `std.time`, `std.wall_time` et `std.random`, avec contrôles
  d'erreur et primitives natives associées ;
- ajout des conversions de texte sûres, du formatage des valeurs primitives et
  d'une API de construction de texte.

### Graphisme et exemples

- ajout du temps de trame, de la limitation du nombre d'images par seconde et
  des modes de fusion au module graphique ;
- ajout d'un exemple complet de jeu Snake néon et de sa documentation.

### Outils et fiabilité

- indexation LSP de l'ensemble d'un projet, y compris les fichiers non ouverts,
  les tests et les dépendances locales ;
- création d'artefacts temporaires sûre en présence de compilations
  concurrentes, avec nettoyage automatique ;
- ajout d'un contrat de stabilité 1.0 et d'un harnais de compatibilité N/N+1 ;
- mise à jour du client de langage VS Code et de ses dépendances afin de
  corriger les alertes de sécurité connues.

## [0.4.0] - 2026-07-24

Cette version renforce le modèle de propriété, consolide les symboles entre
modules et rend les outils plus précis sur les programmes répartis dans
plusieurs fichiers.

### Langage et modèle de propriété

- ajout de la contrainte générique intrinsèque `Copy` ;
- déplacement et destruction récursive des structs et enums propriétaires ;
- initialisation et finalisation sûres des agrégats globaux, y compris en cas
  de panique ;
- évaluation constante des conversions numériques primitives et des
  initialiseurs d'agrégats ;
- contextualisation des littéraux entiers pleine largeur.

### Modules et outils

- qualification cohérente des valeurs, fonctions, types et constructeurs
  d'enums entre modules ;
- visibilité privée étendue aux fonctions externes, types et membres internes ;
- index LSP enrichi pour les imports, symboles masqués et références.

### Bibliothèque standard et graphisme

- sécurisation des collections et itérateurs autour des valeurs copiables ;
- extraction du probing commun aux tables de hachage ;
- découpage de `std.graphics` en sous-modules et uniformisation des couleurs
  autour du type `Color` ;
- masquage des primitives natives internes du backend graphique.

## [0.3.0] - 2026-07-23

Cette version étend le langage numérique, introduit un module graphique 2D
typé et renforce les outils de validation de programmes Janus.

### Langage et bibliothèque standard

- ajout des types `ubyte`, `uint`, `long`, `ulong`, `isize`, `float` et des
  entiers courts, avec une sémantique explicite des conversions et
  débordements ;
- ajout des structures définies et construites directement comme valeurs ;
- ajout des `val` et `var` globales qualifiées par module, avec expressions
  constantes, initialisation dynamique, visibilité privée et destruction
  automatique des valeurs possédées ;
- ajout d'utilitaires mathématiques et de factorisation entière ;
- amélioration des diagnostics pour les retours manquants et les déclarations
  non prises en charge au niveau supérieur.

### Graphisme 2D

- nouveau module graphique typé fondé sur un backend raylib chargé
  dynamiquement ;
- gestion des fenêtres, entrées, textures, sons, musiques, polices UTF-8,
  caméras, sprites, animations, render textures et shaders ;
- ajout d'un script d'installation de raylib et d'une documentation dédiée.

### Outils et fiabilité

- harmonisation de l'interface des commandes d'exécution ;
- ajout d'un corpus Project Euler 1 à 20 et d'un validateur produisant des
  résultats structurés, avec budgets et garde-fous d'exécution ;
- diagnostics LSP corrects pour les modules ne déclarant pas de point d'entrée.

## [0.2.1] - 2026-07-20

Cette version corrective fiabilise les diagnostics du serveur de langage,
l'empaquetage de l'extension VS Code et les mises à jour avec GitHub CLI.

### Serveur de langage

- résolution des imports lors de la production des diagnostics ;
- conservation sûre des messages de diagnostic pendant leur publication ;
- suppression des diagnostics lorsqu'un document est fermé ;
- réponse JSON `null` correcte lorsqu'un symbole demandé est introuvable.

### Extension VS Code

- suppression de l'événement d'activation devenu redondant ;
- ajout des métadonnées, de la licence et des mentions légales au VSIX ;
- bundle minifié limitant l'extension à quelques fichiers ;
- construction et archivage du VSIX dans l'intégration continue.

### Gestionnaire d'installation

- compatibilité avec les installations de GitHub CLI ne prenant pas en charge
  la vérification des attestations.

## [0.2.0] - 2026-07-19

Cette version rend la chaîne d'outils Janus réellement multiplateforme et
améliore fortement l'expérience dans les éditeurs.

### Plateformes et distribution

- ajout des archives officielles macOS ARM64 et Windows x86_64 ;
- validation de l'archive Linux sur Ubuntu 24.04, Fedora 42 et openSUSE
  Tumbleweed ;
- archives autonomes incluant les runtimes natifs nécessaires à Clang et LLD ;
- amélioration de l'installation, de l'activation et des lockfiles sous
  Windows ;
- vérification automatique des attestations de provenance par `janusup`
  lorsque GitHub CLI est disponible.

### Éditeurs et formatage

- ajout des diagnostics en direct dans `janus-lsp` ;
- ajout du survol, de la navigation vers la définition, des références et de
  l'autocomplétion ;
- prise en charge du formatage de document par LSP ;
- configuration de `janus fmt` avec `.janusfmt` et conservation des
  commentaires ;
- ajout d'une extension VS Code avec coloration syntaxique et détection
  automatique de `janus-lsp`.

### Documentation

- nouveau README destiné aux débutants ;
- guides séparés pour l'installation, le langage, les outils et la compilation
  depuis les sources.

### Limites connues

- l'index du serveur LSP reste principalement limité aux documents ouverts ;
- l'extension VS Code est fournie dans le dépôt mais n'est pas encore publiée
  sur la marketplace ;
- le langage, la bibliothèque standard et le format des paquets restent
  expérimentaux avant 1.0.

## [0.1.0] - 2026-07-19

Première version expérimentale de Janus, distribuée pour Linux x86_64.

### Langage

- types primitifs fortement typés, chaînes UTF-8 et conversions explicites ;
- fonctions, closures génériques et valeurs fonctionnelles de premier ordre ;
- classes et enums génériques, visibilité privée et filtrage par motif ;
- collections standard, itérateurs paresseux et builders ;
- contrôle de flux, `defer`, pointeurs bruts et gestion manuelle de la mémoire ;
- interopérabilité C avec les déclarations `extern def`.

### Outils

- compilation native fondée sur LLVM, Clang et LLD ;
- commandes `new`, `init`, `check`, `build`, `run`, `test` et gestion de projet ;
- dépendances locales, Git et registre, résolution SemVer, lockfile et mode
  hors-ligne ;
- gestionnaire de chaînes `janusup`, canaux de mise à jour et installateurs
  Unix/PowerShell ;
- archives autonomes accompagnées d'une somme SHA-256 et d'une attestation de
  provenance.

### Limites connues

- seule une archive Linux x86_64 est publiée dans cette version ;
- le langage, sa bibliothèque standard et le format des paquets restent
  expérimentaux et peuvent évoluer sans compatibilité ascendante avant 1.0.

[0.17.0]: https://github.com/cyril103/janus/releases/tag/v0.17.0
[0.16.0]: https://github.com/cyril103/janus/releases/tag/v0.16.0
[0.15.0]: https://github.com/cyril103/janus/releases/tag/v0.15.0
[0.14.0]: https://github.com/cyril103/janus/releases/tag/v0.14.0
[0.13.0]: https://github.com/cyril103/janus/releases/tag/v0.13.0
[0.12.0]: https://github.com/cyril103/janus/releases/tag/v0.12.0
[0.11.1]: https://github.com/cyril103/janus/releases/tag/v0.11.1
[0.11.0]: https://github.com/cyril103/janus/releases/tag/v0.11.0
[0.10.0]: https://github.com/cyril103/janus/releases/tag/v0.10.0
[0.9.0]: https://github.com/cyril103/janus/releases/tag/v0.9.0
[0.8.1]: https://github.com/cyril103/janus/releases/tag/v0.8.1
[0.8.0]: https://github.com/cyril103/janus/releases/tag/v0.8.0
[0.7.6]: https://github.com/cyril103/janus/releases/tag/v0.7.6
[0.7.4-hotfix.1]: https://github.com/cyril103/janus/compare/v0.7.4...v0.7.4-hotfix.1
[0.7.4]: https://github.com/cyril103/janus/releases/tag/v0.7.4
[0.6.0]: https://github.com/cyril103/janus/releases/tag/v0.6.0
[0.5.0]: https://github.com/cyril103/janus/releases/tag/v0.5.0
[0.4.0]: https://github.com/cyril103/janus/releases/tag/v0.4.0
[0.3.0]: https://github.com/cyril103/janus/releases/tag/v0.3.0
[0.2.1]: https://github.com/cyril103/janus/releases/tag/v0.2.1
[0.2.0]: https://github.com/cyril103/janus/releases/tag/v0.2.0
[0.1.0]: https://github.com/cyril103/janus/releases/tag/v0.1.0
