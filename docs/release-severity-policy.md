# Politique de sévérité des releases

Cette politique s'applique aux versions pré-1.0, aux release candidates et aux
versions 1.x de Janus. Elle complète les critères techniques de la
[roadmap vers 1.0](roadmap-1.0.md).

## Niveaux

- `severity:critical` : corruption de données, double libération, comportement
  indéfini contournant le modèle de propriété, exécution de code ou traversée de
  chemin dans la chaîne d'approvisionnement, violation d'un lockfile, ou crash
  déterministe du compilateur sur un programme valide d'une plateforme tier-1 ;
- `severity:high` : régression majeure sans contournement raisonnable, erreur de
  compilation généralisée ou rupture d'une garantie publique annoncée ;
- `severity:medium` : défaut fonctionnel avec contournement acceptable ou impact
  limité à une surface expérimentale ;
- `severity:low` : défaut mineur, ergonomique ou documentaire sans risque pour
  les programmes produits.

## Gate de release

Toute release est bloquée tant qu'une issue ouverte porte le label
`severity:critical`. Le contrôle échoue également si l'état des issues GitHub ne
peut pas être vérifié : une panne de l'API ne doit pas transformer l'absence de
preuve en autorisation de publier.

Une release candidate 1.0 exige en plus la fermeture des écarts P0 recensés par
l'[audit technique 0.17](audit-0.17.md) et des gates correspondantes dans la
roadmap. Les autres niveaux sont triés explicitement et documentés dans les
notes de version lorsqu'ils restent ouverts.
