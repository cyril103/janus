# Rapport de préparation à Janus 1.0

> Ce rapport conserve la décision historique prise sur le candidat 0.8. Pour
> l'état courant du projet, consulter l'[audit technique 0.17](../audit-0.17.md)
> et la [roadmap vers Janus 1.0](../roadmap-1.0.md).

Date d'audit : 30 juillet 2026. Décision : **NO-GO pour 1.0**.

Le candidat 0.8 apporte un inventaire exhaustif et des gates reproductibles,
mais ne transforme pas le contrat proposé en garantie finale.

## Acquis

- syntaxe, sémantique, ABI C, formats de projet, CLI et stdlib sont classés ;
- les signatures publiques de stdlib et l'aide CLI sont contrôlées contre les
  sources ;
- les fixtures N/N+1 sont compilées par la dernière release et le candidat ;
- lexer, parseur, manifeste et résolution ont un corpus versionné et une
  campagne ASan/UBSan hebdomadaire de 60 minutes chacun ;
- les plateformes tier-1 construisent, testent, emballent et exécutent leurs
  archives.

## Écarts bloquant une 1.0

1. Obtenir plusieurs cycles de retour utilisateur sur le client de registre et
   décider sa politique de cache définitive.
2. Stabiliser ou retirer les surfaces graphiques et publier leur matrice de
   support complète.
3. Figer les garanties LSP/extension qui doivent relever du contrat 1.x.
4. Observer plusieurs campagnes longues sans crash et conserver leurs
   artefacts de reproduction.
5. Clore toute issue `severity:critical` selon la
   [politique 0.8](release-severity-policy-0.8.md), puis dater une revue finale
   du contrat.

La décision 1.0 ne peut devenir GO qu'après résolution explicite de ces cinq
points. Une release nommée 0.8.0 doit rester présentée comme pré-1.0.
