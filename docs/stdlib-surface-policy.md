# Politique de surface de la bibliothèque standard

La bibliothèque standard 1.0 est volontairement plus petite que l'ensemble
des modules livrés. Le statut normatif de chaque module est enregistré dans
l'[inventaire courant](stability-inventory-current.md).

- Le cœur `stable-candidate` couvre les types fondamentaux, erreurs, texte,
  collections usuelles, itération, tests et interfaces système.
- Les modules `experimental` restent disponibles mais ne bénéficient pas de la
  compatibilité source 1.x avant une promotion explicite.
- `std.graphics.*` devient une famille officielle expérimentale hors du cœur ;
  sa future extraction en paquet ne devra pas modifier les garanties du cœur.
- `std.persistent_list`, `std.shared`, `std.validated`, `std.functional`,
  `std.deque` et `std.priority_queue` restent observés sur les projets aval.

Une promotion exige une fixture N/N+1, une documentation complète et un usage
aval. Une suppression passe d'abord par `@deprecated use [[remplacement]]`, le
warning `JANA0033` et une note de migration. Aucun module n'est promu par simple
ancienneté.
