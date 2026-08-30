# Exemple CLI multi-module

Ce projet exerce un manifeste, un module applicatif, la bibliothèque standard
de processus et les tests natifs.

```bash
cd examples/cli-report
janus check --all --deny-warnings
janus run -- alpha beta
janus test
janus doc
```
