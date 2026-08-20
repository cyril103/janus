# Politique de sévérité de release 0.8

> Document historique remplacé par la
> [politique de sévérité courante](../release-severity-policy.md).

Une issue est `severity:critical` si elle démontre au moins un des cas suivants :

- corruption ou double libération de ressource ;
- contournement de propriété menant à un comportement indéfini ;
- exécution de code ou traversée de chemin via la chaîne de paquets ;
- violation reproductible d'un lockfile ;
- crash déterministe du compilateur sur une source Janus valide pour une
  plateforme tier-1.

La gate 0.8 échoue tant qu'une issue ouverte du dépôt porte ce label. La CI
contrôle cet état via l'API GitHub ; une indisponibilité de l'API est un échec
fermé, jamais une validation implicite.
