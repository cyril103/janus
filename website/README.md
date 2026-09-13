# Site d’apprentissage Janus

Site statique officiel de Janus : accueil, book progressif, tutoriels et copie synchronisée de la documentation canonique.

## Développement local

Depuis la racine du dépôt :

```bash
uv venv --python 3.13 website/.venv
uv pip install --python website/.venv/bin/python --require-hashes --only-binary=:all: -r website/requirements.txt
website/.venv/bin/python website/scripts/sync_reference_docs.py
website/.venv/bin/mkdocs serve -f website/mkdocs.yml
```

## Tests et build strict

```bash
python3 -m unittest discover -s website/tests -v
python3 website/tests/check_janus_snippets.py
python3 website/scripts/sync_reference_docs.py
website/.venv/bin/mkdocs build --strict -f website/mkdocs.yml
```

Pour vérifier les routes du site généré :

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory website/site &
server_pid=$!
python3 website/tests/check_public_links.py http://127.0.0.1:8000/
kill "$server_pid"
```

Le workflow Pages construit et teste les pull requests avec uniquement
`contents: read`. Sur les pushes vers `main`, le job `build` transmet l'artefact
au job `deploy`, qui configure Pages puis publie avec `pages: write` et
`id-token: write`. Les tests de `website/tests/test_pages_workflow.py`, exécutés
par la CI du site, contrôlent cette séparation des permissions.

## Nginx avec Docker Compose

Le contexte de build doit rester la racine du dépôt afin d’inclure les documents canoniques :

```bash
docker compose -f website/docker-compose.yml up --build -d
curl -fsS http://127.0.0.1:8080/healthz
```

Changez le port avec `JANUS_SITE_PORT=8090`. Les pages sous `docs/reference/generated/` sont générées et ignorées : modifiez les originaux sous `docs/`.

## Dépendances et mises à jour

`requirements.in` contient les dépendances directes ; `requirements.txt` est le
verrou généré avec toutes les dépendances transitives et leurs hashes SHA-256.
Le conteneur et GitHub Pages imposent `--require-hashes --only-binary=:all:` :
seules les distributions wheel approuvées sont installées, sans compilation de
sources susceptible de télécharger d'autres dépendances de build.
L'étape Python utilise Debian slim pour disposer des wheels Linux de toutes les
dépendances, notamment `watchdog` ; l'image finale nginx reste sous Alpine.

Dependabot ouvre chaque semaine des PR pour les images Docker du site et du
registre, ainsi que pour les dépendances Python (versions et hashes du verrou).
Les tags lisibles restent présents devant les digests pour permettre leur suivi.
Les changements doivent être revus avant fusion ; les hashes identifient les
artefacts approuvés, ils ne garantissent pas à eux seuls leur innocuité.

Pour régénérer le verrou sous Python 3.13 après modification de `requirements.in` :

```bash
uv venv --python 3.13 website/.venv-lock
uv pip install --python website/.venv-lock/bin/python 'pip==25.2' 'pip-tools==7.5.3' 'click==8.2.1'
website/.venv-lock/bin/pip-compile --generate-hashes --strip-extras \
  --no-emit-index-url --pip-args='--only-binary=:all:' \
  --output-file=website/requirements.txt website/requirements.in
```

Ajouter `--upgrade` pour actualiser aussi toutes les dépendances transitives.
Versionner ensemble les fichiers `.in` et `.txt`, puis exécuter les tests et le
build strict ci-dessus. La CI construit aussi l'image du site et vérifie les
références `FROM` de `website/Dockerfile` et `registry/Dockerfile` avec
`python3 scripts/check-docker-pins.py .`. Toute nouvelle image de publication
doit être ajoutée à `PUBLICATION_DOCKERFILES` dans ce script.

## Reconstruire l'image d'un commit

Depuis un clone du dépôt, remplacer le SHA ci-dessous par le commit complet voulu :

```bash
commit=0123456789abcdef0123456789abcdef01234567
git worktree add --detach ../janus-site-rebuild "$commit"
docker build --pull --no-cache --platform=linux/amd64 \
  -f ../janus-site-rebuild/website/Dockerfile \
  -t "janus-website:$commit" ../janus-site-rebuild
docker run --rm -p 8080:80 "janus-website:$commit"
```

La plateforme doit correspondre à celle de l'image à reconstruire. Les commits
contenant ce verrouillage réutilisent les mêmes images de base et artefacts
Python tant qu'ils restent disponibles sur les registres. Cela fixe les entrées
externes du build, sans promettre une image identique octet pour octet (horodatages
et version du moteur de build peuvent différer). Pour restaurer exactement une
image publiée, conserver également son digest et l'artefact dans le registre.
