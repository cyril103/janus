# Canaris downstream Janus8 et Janus Studio

Les publications stable et nightly épinglent Janus8 au commit complet
`b962b69ea14a47016556f9e2a111c7dab1e2e02d` et Janus Studio au commit
`d0e88543427d462df9c34d74c0800a83f55a21ec`. Le gate extrait exclusivement
l’archive Linux candidate du workflow courant, valide son checksum SHA-256, son
identité et la stdlib,
puis exécute `fmt --check`, `check --all --deny-warnings`,
`test --fail-if-empty`, `build`, les gardes de syntaxe native et leur test de
mutation, puis le smoke CLI sur Janus8. Sur Janus Studio, il exécute
`fmt --check`, `check --all`, les 51 tests en mode release et un build release.
Le contrôle Studio n'active pas encore `--deny-warnings` à cause du diagnostic
`JANA0014` connu sur `cursorColumn`; le reste de la matrice est bloquant.
Il précède toute publication ou promotion de canal.

Le canari s’exécute avec un `HOME`, un cache, un registre et des répertoires XDG
temporaires. Il refuse liens, fichiers spéciaux, chemins ambigus et archives
dépassant les limites d’extraction ; aucune installation hôte ne peut masquer un
paquet incomplet.

Les révisions épinglées ne doivent être modifiées que dans
`scripts/downstream_canary.py` et dans les deux workflows, dans une PR qui montre
le passage des canaris avec les archives candidates. Les applications aval ne
doivent jamais être reconstruites avec une toolchain Janus issue des sources
pendant ce gate.
