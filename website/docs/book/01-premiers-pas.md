<span class="chapter-kicker">CHAPITRE 01 / DÉMARRER</span>
# Premiers pas

## Objectifs

- installer et vérifier Janus ;
- créer la structure d’un projet ;
- comprendre le rôle de `main` et du code de sortie.

## Installer et vérifier

Suivez la commande adaptée à votre système dans le [guide d’installation](../reference/generated/getting-started.md), puis vérifiez :

```bash
janus --version
janusup list
```

Créez ensuite un projet :

```bash
janus new bonjour
cd bonjour
janus run
```

Le point d’entrée est `src/main.janus` :

```janus
// doctest: doctest name=hello-world
def main() : int {
    println("Bonjour depuis Janus !")
    return 0
}
```

`main` ne reçoit aucun paramètre. Le retour `0` indique le succès ; une autre valeur peut signaler une erreur à l’environnement.

## La boucle de travail

```bash
janus check          # analyse sans produire d’exécutable
janus run            # compile puis exécute
janus build          # écrit sous target/debug
janus test           # exécute les programmes de tests
janus fmt            # formate src/ et tests/
```

## Exercice

Modifiez le programme pour afficher deux lignes, puis retourner le code de succès.

??? success "Correction"
    ```janus
    def main() : int {
        println("Bonjour depuis Janus !")
        println("Mon premier programme natif.")
        return 0
    }
    ```

<div class="lesson-nav"><span></span><a href="../02-valeurs-types/">Valeurs et types →</a></div>
