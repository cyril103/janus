# Janus Learning Website Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Construire un site officiel francophone de Janus réunissant une présentation claire, la documentation de référence, un livre progressif et des tutoriels pratiques, servi en production par Nginx avec Docker Compose.

**Architecture:** Le site sera statique et généré avec MkDocs Material afin de fournir navigation, recherche plein texte, ancres, responsive et thèmes clair/sombre sans backend. Les nouveaux contenus pédagogiques vivront dans `website/docs`; un script copiera au build les documents canoniques de `docs/` vers une section Référence afin d’éviter une seconde source de vérité. Une image Docker multi-stage construira le site puis le servira avec Nginx.

**Tech Stack:** MkDocs Material, Markdown, CSS/JavaScript sans framework, Python standard library pour la synchronisation et les tests, Docker Compose, Nginx.

---

### Task 1: Mettre en place le générateur et le serveur

**Objective:** Obtenir une image reproductible qui construit puis sert le site.

**Files:**
- Create: `website/mkdocs.yml`
- Create: `website/requirements.txt`
- Create: `website/Dockerfile`
- Create: `website/docker-compose.yml`
- Create: `website/nginx.conf`
- Create: `website/.dockerignore`
- Create: `website/.gitignore`
- Create: `website/README.md`

**Steps:**
1. Déclarer la navigation, la recherche française, les extensions Markdown, les thèmes clair/sombre et les assets dans MkDocs.
2. Créer une construction Docker multi-stage avec dépendances épinglées.
3. Configurer Nginx avec `try_files`, compression, cache des assets, en-têtes de sécurité et `/healthz`.
4. Documenter les commandes locales et Docker exactes.
5. Vérifier `docker compose config` et un build MkDocs local.

### Task 2: Synchroniser la documentation canonique

**Objective:** Publier les documents du dépôt sans les maintenir en double.

**Files:**
- Create: `website/scripts/sync_reference_docs.py`
- Generate (ignored): `website/docs/reference/generated/*.md`
- Create: `website/docs/reference/index.md`
- Create: `website/tests/test_site.py`

**Steps:**
1. Définir explicitement les documents copiés et leur ordre.
2. Ajouter un en-tête indiquant leur provenance canonique.
3. Réécrire les liens internes au dépôt qui ne font pas partie du site vers GitHub.
4. Tester la synchronisation, la présence des pages et l’absence de liens locaux cassés.

### Task 3: Créer l’identité et l’accueil

**Objective:** Donner au langage une présence éditoriale originale et immédiatement utile.

**Files:**
- Create: `website/docs/index.md`
- Create: `website/docs/assets/logo.svg`
- Create: `website/docs/assets/favicon.svg`
- Create: `website/docs/stylesheets/extra.css`
- Create: `website/docs/javascripts/extra.js`

**Steps:**
1. Composer une page d’accueil asymétrique orientée apprentissage, sans grille marketing générique.
2. Présenter honnêtement Janus 0.5, son caractère expérimental et ses plateformes.
3. Ajouter installation, premier programme et trois parcours d’entrée : découvrir, apprendre, approfondir.
4. Créer un système visuel sombre/clair, typographique, accessible et responsive.
5. Ajouter la copie des blocs de code avec retour visuel et respect de `prefers-reduced-motion`.

### Task 4: Écrire le livre progressif

**Objective:** Permettre à une personne débutante en Janus de progresser de zéro à un petit projet structuré.

**Files:**
- Create: `website/docs/book/index.md`
- Create: `website/docs/book/01-premiers-pas.md`
- Create: `website/docs/book/02-valeurs-types.md`
- Create: `website/docs/book/03-controle-fonctions.md`
- Create: `website/docs/book/04-modeliser-donnees.md`
- Create: `website/docs/book/05-erreurs-propriete.md`
- Create: `website/docs/book/06-collections-iterateurs.md`
- Create: `website/docs/book/07-projets-tests-outils.md`
- Create: `website/docs/book/08-projet-final.md`

**Steps:**
1. Donner à chaque chapitre objectifs, concepts, exemples exécutables, exercice et correction repliable.
2. Introduire les notions dans un ordre cohérent, sans promettre de fonctionnalité absente.
3. Terminer par un projet CLI complet consolidant le cursus.
4. Valider les extraits Janus essentiels avec le compilateur disponible.

### Task 5: Ajouter des tutoriels orientés résultat

**Objective:** Fournir des parcours indépendants et concrets.

**Files:**
- Create: `website/docs/tutorials/index.md`
- Create: `website/docs/tutorials/cli-compteur.md`
- Create: `website/docs/tutorials/collections.md`
- Create: `website/docs/tutorials/gestion-erreurs.md`
- Create: `website/docs/tutorials/snake-graphique.md`

**Steps:**
1. Structurer chaque tutoriel avec prérequis, résultat, étapes, code, vérification et prolongements.
2. Réutiliser les exemples réels du dépôt et l’API actuelle de Janus 0.5.
3. Signaler clairement la dépendance raylib du tutoriel graphique.

### Task 6: Vérifier, revoir et livrer

**Objective:** Prouver que le site est navigable, exact et déployable.

**Steps:**
1. Exécuter le script de synchronisation et les tests standard-library.
2. Construire MkDocs en mode strict et vérifier les liens du HTML généré.
3. Lancer le service disponible, tester `/`, une page du book, une page de référence et `/healthz`.
4. Vérifier le rendu desktop/mobile dans le navigateur et l’absence d’erreurs console.
5. Faire une revue contenu/UX, corriger les problèmes, puis exécuter `git diff --check`.
6. Commit et push de la branche `feat/learning-website` après validation.
