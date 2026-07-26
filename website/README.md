# Site d’apprentissage Janus

Site statique officiel de Janus : accueil, book progressif, tutoriels et copie synchronisée de la documentation canonique.

## Développement local

Depuis la racine du dépôt :

```bash
uv venv website/.venv
uv pip install --python website/.venv/bin/python -r website/requirements.txt
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

## Nginx avec Docker Compose

Le contexte de build doit rester la racine du dépôt afin d’inclure les documents canoniques :

```bash
docker compose -f website/docker-compose.yml up --build -d
curl -fsS http://127.0.0.1:8080/healthz
```

Changez le port avec `JANUS_SITE_PORT=8090`. Les pages sous `docs/reference/generated/` sont générées et ignorées : modifiez les originaux sous `docs/`.
