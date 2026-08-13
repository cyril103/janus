# Canari downstream Janus8

Les publications stable et nightly épinglent Janus8 au commit complet
`b962b69ea14a47016556f9e2a111c7dab1e2e02d`. Le gate extrait exclusivement
l’archive Linux candidate du workflow courant, valide son checksum SHA-256, son
identité et la stdlib,
puis exécute `fmt --check`, `check --all --deny-warnings`,
`test --fail-if-empty`, `build`, les gardes de syntaxe native et leur test de
mutation, puis le smoke CLI.
Il précède toute publication ou promotion de canal.

Le canari s’exécute avec un `HOME`, un cache, un registre et des répertoires XDG
temporaires. Il refuse liens, fichiers spéciaux, chemins ambigus et archives
dépassant les limites d’extraction ; aucune installation hôte ne peut masquer un
paquet incomplet.

La révision épinglée ne doit être modifiée que dans
`scripts/downstream_canary.py` et dans les deux workflows, dans une PR qui montre
le passage du canari avec les archives candidates. Janus8 ne doit jamais être
reconstruit avec une toolchain Janus issue des sources pendant ce gate.
