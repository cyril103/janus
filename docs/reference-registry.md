# Registre Janus de référence : exploitation et réponse à incident

Statut : **référence déployable pour Janus 0.8.0**. Le service implémente le
[protocole Registry v1](registry-protocol-v1.md) sans étendre ses objets JSON
fermés. Les ressources de provenance et d'audit ci-dessous sont des extensions
du service de référence, pas des champs du protocole v1.

## Architecture et garanties

Le service dans [`registry/`](../registry/) utilise uniquement Python 3.13 et
SQLite :

- les métadonnées, manifestes et archives sont des blobs adressés par SHA-256 ;
- une transaction rend une version visible après validation complète des trois
  parties et une version existante ne peut jamais être remplacée ;
- les jetons ne sont conservés que sous forme de SHA-256 séparé par domaine ;
- les portées `publish:<namespace>`, `yank:<namespace>` et `audit` séparent les
  autorités ;
- chaque publication reçoit une attestation signée et chaque opération sensible
  produit un événement d'audit signé, chaîné au précédent ;
- SQLite est configuré en WAL avec synchronisation complète ; les blobs sont
  écrits dans un fichier temporaire puis renommés atomiquement.

Le service refuse les archives non canoniques, liens, chemins traversants,
entrées absentes du manifeste, incohérences de taille ou de checksum et
publications multipart incomplètes. Les réponses d'erreur ne réaffichent jamais
les en-têtes d'authentification.

## Modèle de provenance retenu

`GET /v1/packages/{namespace}/{name}/{version}/provenance` retourne un reçu JCS :

```json
{
  "keyId": "registry-2026-01",
  "signature": "base64url-hmac-sha256",
  "statement": {
    "_type": "https://janus-lang.org/provenance/registry-receipt/v1",
    "acceptedAt": "2026-07-30T19:00:01Z",
    "archiveSha256": "...",
    "manifestSha256": "...",
    "metadataSha256": "...",
    "package": "acme/math",
    "publishedAt": "2026-07-30T19:00:00Z",
    "publisher": "release-bot",
    "requestId": "...",
    "version": "1.2.3"
  }
}
```

La signature HMAC-SHA-256 lie l'identité authentifiée, l'identité complète de la
release et les trois empreintes. Ce reçu atteste l'admission par le registre ;
il ne prétend pas remplacer une attestation de build Sigstore. La clé est
montée comme secret, son identifiant est public et sa valeur ne doit jamais
être placée dans l'image, les variables d'environnement, les sauvegardes ou les
journaux.

`GET /v1/audit[?package=namespace/name]` exige la portée `audit`. Chaque événement
contient `previousDigest`, `digest`, `keyId` et `signature`. La commande
`verify-audit` vérifie toute la chaîne.

## Construction et déploiement reproductibles

L'image de base est épinglée par version et digest OCI. BuildKit peut produire
le SBOM et l'attestation de build :

```sh
docker buildx build registry \
  --tag registry.example/janus/reference-registry:0.8.0 \
  --provenance=mode=max --sbom=true --load
```

Préparer un répertoire persistant possédé par l'UID non privilégié `65532`, une
clé de signature d'au moins 32 octets et la configuration :

```sh
sudo install -d -o 65532 -g 65532 -m 0700 /srv/janus-registry
umask 077
openssl rand 32 > /srv/janus-registry-signing.key
cat > registry/.env <<'EOF'
JANUS_REGISTRY_ORIGIN=https://registry.example
JANUS_REGISTRY_BIND=127.0.0.1
JANUS_REGISTRY_DATA_DIR=/srv/janus-registry
JANUS_REGISTRY_KEY_ID=registry-2026-01
JANUS_REGISTRY_SIGNING_KEY_FILE=/srv/janus-registry-signing.key
EOF
docker compose --env-file registry/.env -f registry/compose.yaml up -d
```

Un reverse proxy TLS doit être le seul accès externe au port lié en loopback.
Il doit préserver `X-Request-ID`, limiter le corps à 130 Mio, désactiver les
redirections et ne jamais journaliser `Authorization`. `/healthz` est la sonde
de disponibilité. La base, les blobs et les secrets ne doivent pas résider sur
le même support de sauvegarde.

## Refus anonymes, capacité et limites obligatoires

La validation des publications contrôle chaque fichier avant de poursuivre la
lecture : 10 000 fichiers au maximum, 32 Mio par fichier et 256 Mio au total.
Les extensions PAX/GNU sont limitées à 64 Kio par en-tête, à 8 Mio cumulés
(en-têtes et alignement compris) et à 16 en-têtes consécutifs par fichier.
Les fichiers creux (« sparse ») sont refusés. Le flux décompressé entier,
y compris les données après la fin du tar, est borné à 256 Mio +
10 000 × 1 024 octets + 8 Mio + 10 240 octets, pour inclure les en-têtes et
l’alignement. Un dépassement est refusé avant l’écriture des artefacts de
publication.

Les échecs d'authentification (jeton absent, mal formé, invalide ou révoqué) et
les publications rejetées avant authentification ne sont jamais écrits dans
SQLite ni signés. Ils alimentent seulement dix compteurs en mémoire : cinq
catégories fixes (`read-audit`, `publish`, `yank`, `unyank`,
`invalid-publication`) pour la minute courante et la précédente, mesurées par
l'horloge monotone. Chaque compteur sature à `2^63 - 1`. Aucune adresse IP,
URL, identité de paquet, valeur de jeton ou identifiant de requête ne sert de
clé ; varier ces valeurs ne peut donc pas augmenter la mémoire retenue.

`GET /v1/audit` expose ces agrégats non signés dans `anonymousDenials`, avec
`windowSeconds: 60`, `current` et `previous`, indépendamment du filtre de
paquet. Ils expirent après au plus deux minutes et disparaissent au redémarrage.
Une supervision authentifiée doit les relever toutes les 30 secondes et
alerter dès 100 refus par minute ; ne pas additionner des fenêtres qui se
recouvrent. Aucun export automatique vers un journal disque n'est effectué.
Les refus de portée d'un sujet authentifié restent tous dans la chaîne signée,
avec son sujet, la cible et l'identifiant de requête, comme les publications,
conflits et yanks. Les anciens événements anonymes restent vérifiables.

En production, le proxy TLS doit obligatoirement appliquer, avant transmission :

- une limite par IP et catégorie d'endpoint protégé (lecture d'audit,
  publication, yank/unyank), initialement 2 requêtes/s et une rafale de 10 ;
- un plafond global de 20 requêtes/s sur ces endpoints et 32 connexions
  simultanées vers le service, pour borner aussi une attaque distribuée ;
- une réponse `429` aux dépassements, des délais de lecture de 15 secondes et
  la limite de corps de 130 Mio ; ajuster le délai des transferts autorisés
  à la taille des archives et au débit minimal accepté.

Les clés de limitation doivent regrouper les chemins normalisés par catégorie,
sans créer un quota distinct pour chaque nom de paquet. Inclure les chemins
invalides et les variantes encodées ; l'adresse client provient de la connexion
ou d'un proxy explicitement approuvé, jamais d'un en-tête libre du client.
Valider au déploiement qu'une rafale de 100 requêtes anonymes produit des `429`,
que changer le nom du paquet ne réinitialise pas le quota et qu'un faux
`X-Forwarded-For` ne permet pas de l'éviter. Vérifier aussi `/healthz` et une
opération autorisée depuis une autre IP pendant cette rafale.

Prévoir un volume dédié avec quota (point de départ : 10 Gio, à dimensionner
selon les archives et l'activité authentifiée). Surveiller chaque minute la
base, le WAL, les blobs, l'espace libre et la latence des écritures ; alerter
à 70 % du quota, déclencher une intervention à 85 % ou si la latence dépasse
le budget de service. Le quota protège le reste de l'hôte mais sa saturation
peut empêcher des publications : augmenter la capacité avant ce seuil.
Conserver les sauvegardes quotidiennes chiffrées 90 jours sur un autre volume,
avec leur propre quota et une alerte sur les échecs de sauvegarde. Borner les
journaux du proxy par rotation (par exemple 7 jours et 100 Mio au total), sans
en-têtes sensibles ni paramètres de requête.

La chaîne métier reste conservée intégralement dans la base active : ne pas
supprimer ses lignes pour appliquer une rétention, ce qui casserait sa
vérification. Aucun élagage cryptographique n'est implémenté ; prévoir sa
croissance selon l'activité authentifiée et révoquer les jetons abusifs.
Le test `registry.reference` envoie 840 requêtes hostiles avec huit clients,
vérifie une croissance SQLite nulle pendant un verrou d'écriture délibéré,
puis des écritures légitimes et des refus authentifiés sous bruit concurrent,
et vérifie la chaîne après sauvegarde/restauration. Ce test borné est une
régression de stockage et de contention, pas une garantie de débit : mesurer
la capacité réelle sur le matériel de production. L'application supprime
l'amplification disque anonyme ; le proxy borne le coût CPU, réseau et threads.

## Jetons et autorités

Générer un jeton aléatoire, le transmettre au sujet par un canal distinct, puis
le fournir sur l'entrée standard. La commande affiche seulement son empreinte,
utile pour une révocation ultérieure :

```sh
umask 077
openssl rand -base64 36 |
  PYTHONPATH=registry python3 -m reference_registry.admin provision-token \
    --data /srv/janus-registry \
    --signing-key-file /srv/janus-registry-signing.key \
    --key-id registry-2026-01 \
    --subject release-bot \
    --scope publish:acme --scope yank:acme
```

Un compte d'audit reçoit uniquement `--scope audit`. Pour révoquer :

```sh
PYTHONPATH=registry python3 -m reference_registry.admin revoke-token \
  --data /srv/janus-registry \
  --signing-key-file /srv/janus-registry-signing.key \
  --key-id registry-2026-01 \
  --digest EMPREINTE_A_REVOQUER
```

## Yank, audit et administration

Le yank conserve tous les octets et modifie seulement la résolution fraîche :

```sh
curl --fail-with-body -X POST \
  -H "Authorization: Bearer $JANUS_REGISTRY_TOKEN" \
  -H 'Content-Type: application/vnd.janus.registry.v1+json' \
  --data-binary '{"reason":"release compromise"}' \
  https://registry.example/v1/packages/acme/math/1.2.3/yank
```

`DELETE` sur la même URL restaure la résolution. Une version yanked reste
téléchargeable par identité et checksum pour les lockfiles existants. Vérifier
régulièrement le journal :

```sh
PYTHONPATH=registry python3 -m reference_registry.admin verify-audit \
  --data /srv/janus-registry \
  --signing-key-file /srv/janus-registry-signing.key \
  --key-id registry-2026-01
```

## Sauvegarde et restauration

Une sauvegarde en ligne utilise l'API snapshot de SQLite, puis copie uniquement
les blobs référencés. L'archive déterministe contient un manifeste de checksums,
les autorités, les attestations et le journal, mais aucune clé ni valeur de
jeton :

```sh
PYTHONPATH=registry python3 -m reference_registry.admin backup \
  --data /srv/janus-registry \
  --signing-key-file /srv/janus-registry-signing.key \
  --key-id registry-2026-01 \
  --output /srv/backups/janus-registry-$(date -u +%Y%m%dT%H%M%SZ).tar.gz
```

Chiffrer ensuite l'archive, la copier hors ligne et appliquer une rétention
indépendante. Un exercice de restauration utilise toujours une nouvelle racine :

```sh
PYTHONPATH=registry python3 -m reference_registry.admin restore \
  --archive /srv/backups/janus-registry-20260730T190000Z.tar.gz \
  --data /srv/janus-registry-restored

PYTHONPATH=registry python3 -m reference_registry.admin verify-audit \
  --data /srv/janus-registry-restored \
  --signing-key-file /srv/janus-registry-signing.key \
  --key-id registry-2026-01
```

Démarrer une instance isolée sur cette racine, vérifier recherche, métadonnées,
provenance et téléchargement, puis basculer atomiquement la configuration du
service vers la nouvelle racine. Ne jamais restaurer par-dessus une racine
active. Le test `registry.reference` automatise publication, refus d'accès,
immutabilité, yank, audit, sauvegarde, restauration et build
`--locked --offline` après arrêt du registre.

## Réponse à incident

1. Isoler le service et conserver une copie en lecture seule de la base, des
   blobs, journaux du proxy et identifiants de requête.
2. Révoquer les jetons concernés avant toute réouverture. Ne jamais publier une
   correction sous une version existante.
3. Vérifier la chaîne d'audit et les SHA-256 de la dernière sauvegarde saine,
   puis restaurer dans une nouvelle racine isolée.
4. Yank les releases compromises, publier une nouvelle version et prévenir les
   consommateurs avec les identités et checksums affectés.
5. Faire tourner la clé de signature en conservant l'ancienne hors ligne pour
   vérifier les reçus historiques ; déployer un nouveau `keyId`.
6. Tester un projet dont le cache a déjà été validé avec
   `janus check --locked --offline` avant la remise en ligne.
7. Documenter chronologie, portée, révocations, restaurations et mesures
   correctives sans inclure de secret.
