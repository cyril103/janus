# Protocole du registre Janus v1

Statut : **normatif pour le protocole v1, expérimental dans Janus 0.7.9**. Ce
contrat précède volontairement le client distant et le service de référence.
Les mots **DOIT**, **NE DOIT PAS** et **DEVRAIT** ont leur sens normatif usuel.
Les schémas JSON associés vivent dans [`docs/schemas/registry-v1`](schemas/registry-v1/)
et les exemples exécutables dans
[`tests/fixtures/registry-v1`](../tests/fixtures/registry-v1/).

## Version et négociation

Toutes les ressources JSON portent `protocolVersion: "1"`, utilisent UTF-8 et
`Content-Type: application/vnd.janus.registry.v1+json`. Un client envoie
`Accept` avec ce type. Le serveur répond `406 Not Acceptable` s'il ne peut pas
servir une version acceptée et `415 Unsupported Media Type` à une publication
dans un format inconnu. Aucun repli silencieux n'est permis. Les schémas v1
sont fermés : tout nouveau champ ou changement de sens exige une nouvelle
version explicitement négociée.

Le client découvre les versions sans supposer `v1` via
`GET /.well-known/janus-registry`, avec `Accept: application/json`. La réponse
est conforme à `discovery.schema.json`, annonce les versions supportées et une
base HTTPS par version. Le client choisit la version la plus haute qu'il prend
explicitement en charge ; une intersection vide est une erreur, jamais un repli.

## Noms et espaces de noms

L'identité canonique est `namespace/name`, en ASCII minuscule, selon
`^[a-z][a-z0-9_-]{0,63}/[a-z][a-z0-9_-]{0,63}$`. La comparaison est octet par
octet : aucune normalisation Unicode, casse ou alias implicite. Un espace de
noms appartient à une autorité du registre. La réservation, le transfert et la
suppression d'une autorité sont audités. Le tuple `(registry, namespace/name,
version)` est l'identité complète ; un nom obtenu d'un autre registre n'est
jamais un candidat équivalent.

L'identifiant canonique du registre est son origine HTTPS suivie de son éventuel
préfixe de base : schéma et hôte en minuscules, hôte IDNA ASCII, aucun port
explicite dans le protocole v1, aucun userinfo, query, fragment, segment `.`/`..`
ni slash final. Les caractères non réservés percent-encodés sont décodés. Toute
comparaison de registre utilise exclusivement cette forme ; une URL non
canonique est rejetée.

## Ressources HTTP v1

Les segments sont encodés séparément ; après décodage, tout segment contenant
`/`, `\`, NUL, `.` ou `..` est rejeté.

| Méthode et ressource | Réponse / corps normatif |
| --- | --- |
| `GET /.well-known/janus-registry` | négociation conforme à `discovery.schema.json` |
| `GET /v1/search?q={texte}` | résultats publics conformes à `search.schema.json`, triés de façon stable par identité canonique |
| `GET /v1/packages/{namespace}/{name}` | index conforme à `index.schema.json` |
| `GET /v1/packages/{namespace}/{name}/{version}/metadata` | métadonnées conformes à `metadata.schema.json` |
| `GET /v1/packages/{namespace}/{name}/{version}/archive.tar.gz` | archive gzip déterministe dont SHA-256 et taille correspondent aux métadonnées |
| `GET /v1/packages/{namespace}/{name}/{version}/archive-manifest` | manifeste conforme à `archive-manifest.schema.json` |
| `PUT /v1/packages/{namespace}/{name}/{version}` | publication atomique authentifiée, métadonnées + manifeste + archive |
| `POST /v1/packages/{namespace}/{name}/{version}/yank` | changement du seul état de résolution |
| `DELETE /v1/packages/{namespace}/{name}/{version}/yank` | restauration du seul état de résolution |

La recherche est informative et ne participe jamais à la résolution. Le
paramètre `q` est UTF-8 encodé dans la query, ne contient aucun secret et le
serveur peut rechercher identité et description. Une installation repart
toujours de l'identité canonique retournée et de son index vérifié.

Les réponses JSON sont sérialisées selon RFC 8785 (JCS) avant calcul d'une
empreinte ; les consommateurs vérifient les octets canoniques, pas une
réinterprétation des champs. Les réponses de contenu ont `ETag: "<sha256>"`, où
`<sha256>` est le SHA-256 hexadécimal minuscule de ces octets canoniques. Le
client vérifie métadonnées, manifeste, taille et archive avant extraction.

La publication `PUT` utilise `multipart/related` avec exactement trois parties
nommées par `Content-ID`: `metadata` (JSON JCS), `archive-manifest` (JSON JCS)
et `archive` (`application/gzip`). Les identités et empreintes des trois parties
doivent concorder. Le yank reçoit un objet JCS `{ "reason": "..." }`; le sujet
et la date proviennent de l'identité authentifiée et de l'horloge du serveur.
Les erreurs normatives sont `400` (corps incohérent), `401` (authentification),
`403` (autorité), `409` (version existante) et `422` (archive non sûre).

## Publication immuable et yanking

Une publication utilise `If-None-Match: "*"`. Le serveur réserve l'identité,
valide tout le lot, puis le rend visible atomiquement. Si la version existe, il
retourne `409 Conflict`, même si les octets proposés sont identiques : une
version publiée ne peut être remplacée, réassignée ni supprimée physiquement
par l'API normale.

Le yanking est un marqueur mutable et audité sur l'index, pas une mutation des
métadonnées ou de l'archive. Une nouvelle résolution ignore une version yanked,
mais un lockfile existant peut encore la télécharger par identité et checksums
exacts. Un administrateur peut bloquer ce téléchargement uniquement pour une
raison légale ou une compromission critique ; le registre conserve alors les
métadonnées et l'événement, et le build échoue explicitement plutôt que de
choisir une autre version.

## Résolution et lockfile

La résolution transporte toujours le registre demandé. Chaque dépendance des
métadonnées donne aussi son registre explicite. Le client rejette tout résultat
dont le registre ou le nom diffère de la demande : c'est la défense normative
contre la **dependency confusion**.

Une résolution fraîche choisit la plus haute version SemVer satisfaisant la
contrainte dans le registre demandé, après exclusion des versions yanked. Une
préversion n'est candidate que si la contrainte mentionne explicitement une
préversion. Il n'existe ni priorité implicite de registre, ni fallback public,
ni sélection par date ; l'indisponibilité de la source demandée est une erreur.

`janus.lock` reste la source reproductible. Pour une dépendance distante, il
épingle au minimum protocole, URL canonique du registre, nom canonique, version,
SHA-256 des métadonnées et SHA-256 de l'archive, selon
`resolution.schema.json`. En `--locked`, aucun index ni yank ne peut modifier
ce choix. En `--offline`, seuls des octets déjà en cache et vérifiés contre ces
empreintes sont admis. Une version yanked reste donc reproductible selon la
politique précédente, sans résolution flottante.

## Archives et extraction sûre

L'archive contient des fichiers réguliers uniquement. Le manifeste liste chaque
entrée une fois, avec chemin relatif canonique, taille et SHA-256. Sont interdits
les chemins absolus, vides, dupliqués, `.`/`..`, les antislashs, liens matériels
ou symboliques, périphériques, FIFOs, propriétaires spéciaux et entrées non
listées. L'extracteur ouvre une destination confinée, vérifie après normalisation
que chaque cible reste sous cette racine et applique des limites de nombre,
taille individuelle et taille totale. Ces règles empêchent la **path traversal**
et les archives de décompression abusive.

## Authentification et autorisation

Les lectures publiques ne requièrent pas de secret. Publication, yank et
dé-yank utilisent TLS et un jeton court ou une identité fédérée. Les secrets
sont transmis uniquement dans `Authorization`; un client supprime cet en-tête
et refuse toute redirection cross-origin. Le serveur stocke seulement une
empreinte de jeton, exige les portées `publish:<namespace>` et
`yank:<namespace>`, vérifie l'autorité du namespace et journalise sujet,
action, paquet, résultat et identifiant de requête. Les journaux, réponses,
lockfiles et URLs NE DOIVENT PAS contenir de jeton. Les actions d'administration
sensibles exigent une identité distincte et une authentification renforcée.

## Modèle de menaces

| Menace | Contrôle v1 |
| --- | --- |
| substitution ou rejeu | TLS, identité complète, SHA-256 et immutabilité |
| dependency confusion | registre et namespace épinglés, aucune équivalence inter-registre |
| path traversal / archive hostile | manifeste exhaustif, chemins confinés, types et tailles limités |
| compte éditeur compromis | jetons courts et limités, journal, révocation, yank sans remplacement |
| serveur ou stockage compromis | checksums du lockfile, sauvegardes immuables, audit de restauration |
| indisponibilité | cache vérifié et builds `--locked --offline` |

SHA-256 protège l'intégrité et non l'identité de l'éditeur. La provenance signée
pourra être ajoutée sans remplacer les contrôles v1 ; sa politique appartient au
registre de référence de la release suivante.

## Sauvegarde et récupération

Le stockage sépare blobs immuables et journal append-only des index/yanks. Une
sauvegarde cohérente contient blobs, métadonnées, autorités de namespace,
journal d'audit et version du schéma. Les sauvegardes sont chiffrées, à accès
restreint, avec rétention et copie hors ligne. Une restauration est faite dans
un environnement isolé, puis vérifie tous les SHA-256, reconstruit les index à
partir du journal et compare un échantillon aux lockfiles fixtures avant bascule
atomique. Les exercices de restauration sont périodiques et consignés.

Après incident, les clés et jetons sont révoqués avant réouverture. Les versions
publiées ne sont jamais corrigées en place : on restaure les octets historiques
valides, on yank une version compromise et on publie une nouvelle version.
