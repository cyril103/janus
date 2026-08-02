# Référence de la bibliothèque standard

Cette page accompagne la référence générée par `janus doc --stdlib`. Chaque
module possède ici un exemple minimal compilé par `janus test --doc`. Les
exemples `compile_fail` verrouillent les usages qui doivent traiter une absence
ou une erreur structurée au lieu de l’ignorer implicitement.

## Modules fondamentaux

### `std.array`

```janus
// doctest: doctest name=stdlib-std-array
import std.array
def main() : int { return 0 }
```

### `std.array_builder`

```janus
// doctest: doctest name=stdlib-std-array-builder
import std.array_builder
def main() : int { return 0 }
```

### `std.builder`

```janus
// doctest: doctest name=stdlib-std-builder
import std.builder
def main() : int { return 0 }
```

### `std.iterator`

```janus
// doctest: doctest name=stdlib-std-iterator
import std.iterator
def main() : int { return 0 }
```

### `std.option`

```janus
// doctest: doctest name=stdlib-std-option
import std.option
def main() : int { return 0 }
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-option-requires-handling
import std.option
def main() : int {
    val missing : Option[int] = Option.None[int]()
    return missing
}
```

### `std.range`

```janus
// doctest: doctest name=stdlib-std-range
import std.range
def main() : int { return 0 }
```

### `std.result`

```janus
// doctest: doctest name=stdlib-std-result
import std.result
def main() : int { return 0 }
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-result-requires-handling
import std.result
def main() : int {
    val failed : Result[int, int] = Result.Error[int, int](1)
    return failed
}
```

## Collections et hachage

### `std.hash_probe`

Ce module est exercé ici comme détail d'implémentation des collections. Il ne
fait pas partie du candidat public stable de Janus 0.8.

```janus
// doctest: doctest name=stdlib-std-hash-probe
import std.hash_probe
def main() : int {
    return if normalizedHashCapacity(usize(1)) == usize(8) { 0 } else { 1 }
}
```

### `std.hashing`

```janus
// doctest: doctest name=stdlib-std-hashing
import std.hashing
def main() : int { return 0 }
```

### `std.hashmap`

```janus
// doctest: doctest name=stdlib-std-hashmap
import std.hashmap
def main() : int { return 0 }
```

### `std.hashset`

```janus
// doctest: doctest name=stdlib-std-hashset
import std.hashset
def main() : int { return 0 }
```

## Système, texte et temps

### `std.c`

```janus
// doctest: doctest name=stdlib-std-c
import std.c
def main() : int { return if strlen(cstr("Janus")) == usize(5) { 0 } else { 1 } }
```

### `std.system`

```janus
// doctest: doctest name=stdlib-std-system
import std.system
def main() : int {
    return if SystemOpenMode.Read == SystemOpenMode.Read { 0 } else { 1 }
}
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-system-requires-handling
import std.system
def main() : int {
    return openSystemFile("missing.txt", SystemOpenMode.Read)
}
```

### `std.path`

```janus
// doctest: doctest name=stdlib-std-path
import std.path
def main() : int { return 0 }
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-path-requires-handling
import std.path
def main() : int { return normalizePath(".") }
```

### `std.fs`

```janus
// doctest: doctest name=stdlib-std-fs
import std.fs
def main() : int {
    return if FileType.File == FileType.File { 0 } else { 1 }
}
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-fs-requires-handling
import std.fs
def main() : int { return readFile("missing.txt") }
```

### `std.io`

```janus
// doctest: doctest name=stdlib-std-io
import std.io
def main() : int {
    return if TextDecodeError.InvalidUtf8 == TextDecodeError.InvalidUtf8 {
        0
    } else {
        1
    }
}
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-io-requires-handling
import std.io
def main() : int { return openInputStream("missing.txt") }
```

### `std.process`

```janus
// doctest: doctest name=stdlib-std-process
import std.process
def main() : int {
    val count : usize = programArgumentCount()
    return if count == programArgumentCount() { 0 } else { 1 }
}
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-process-requires-handling
import std.process
def main() : int { return environmentVariable("PATH") }
```

### `std.text`

```janus
// doctest: doctest name=stdlib-std-text
import std.text
def main() : int { return 0 }
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-text-requires-handling
import std.text
def main() : int { return parseInt("not-an-int") }
```

### `std.testing`

Assertions destinées aux fonctions `/// @test`. Les échecs déclenchent une
panique isolée par `janus test` et affichent les valeurs lorsque `Debug` est
disponible.

```janus
// doctest: doctest name=stdlib-std-testing
import std.testing
import std.option
assertEqual[int](2 + 2, 4)
assertSome[int](Option.Some[int](42))
```

```janus
// doctest: compile_fail=JANA0001 name=stdlib-std-testing-requires-import
assertTrue(unknownCondition)
```

### `std.math`

```janus
// doctest: doctest name=stdlib-std-math
import std.math
def main() : int { return if gcd(usize(42), usize(30)) == usize(6) { 0 } else { 1 } }
```

### `std.random`

```janus
// doctest: doctest name=stdlib-std-random
import std.random
def main() : int { return 0 }
```

### `std.time`

```janus
// doctest: doctest name=stdlib-std-time
import std.time
def main() : int {
    return if seconds(usize(1)).seconds() == 1.0 { 0 } else { 1 }
}
```

### `std.wall_time`

```janus
// doctest: doctest name=stdlib-std-wall-time
import std.wall_time
def main() : int { return 0 }
```

## Graphisme et audio expérimentaux

### `std.graphics`

```janus
// doctest: doctest name=stdlib-std-graphics
import std.graphics
def main() : int { return 0 }
```

### `std.graphics.audio`

```janus
// doctest: doctest name=stdlib-std-graphics-audio
import std.graphics.audio
def main() : int { return 0 }
```

### `std.graphics.drawing`

```janus
// doctest: doctest name=stdlib-std-graphics-drawing
import std.graphics.drawing
def main() : int { return 0 }
```

### `std.graphics.input`

```janus
// doctest: doctest name=stdlib-std-graphics-input
import std.graphics.input
def main() : int { return 0 }
```

### `std.graphics.resources`

```janus
// doctest: doctest name=stdlib-std-graphics-resources
import std.graphics.resources
def main() : int { return 0 }
```

### `std.graphics.types`

```janus
// doctest: doctest name=stdlib-std-graphics-types
import std.graphics.types
def main() : int {
    return if rgba(ubyte(0), ubyte(0), ubyte(0), ubyte(255)) == Black {
        0
    } else {
        1
    }
}
```
