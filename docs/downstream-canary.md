# Canaris downstream Janus8 et Janus Studio

Les publications stable et nightly épinglent Janus8 au commit complet
`576487e68caa341d57dadd7c203f502905ccae57` et Janus Studio au commit
`c05410c831e31645c1b5801f888154ef684fc4ba`. Le gate extrait exclusivement
l’archive Linux candidate du workflow courant, valide son checksum SHA-256, son
identité et la stdlib,
puis exécute `fmt --check`, `check --all --deny-warnings`,
`test --fail-if-empty`, `build`, les gardes de syntaxe native et leur test de
mutation, puis le smoke CLI sur Janus8. Sur Janus Studio, il exécute
`fmt --check`, `check --all`, les 70 tests en mode release et un build release.
Toute cette matrice est bloquante.
Il précède toute publication ou promotion de canal.

Le canari s’exécute avec un `HOME`, un cache, un registre et des répertoires XDG
temporaires. Il refuse liens, fichiers spéciaux, chemins ambigus et archives
dépassant les limites d’extraction ; aucune installation hôte ne peut masquer un
paquet incomplet.

Les révisions épinglées doivent rester synchronisées dans ce document,
`scripts/downstream_canary.py` et les deux workflows. Elles ne sont modifiées que
dans une PR qui montre le passage des canaris avec les archives candidates. Les
applications aval ne doivent jamais être reconstruites avec une toolchain Janus
issue des sources pendant ce gate.
