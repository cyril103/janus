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
def main() : int {
    val values : Array[int] = [1, 2, 3]
    val sum : int = values.fold[int](
        0,
        (total : int, value : int) => total + value
    )
    delete values
    return sum - 6
}
```

### `std.array_builder`

```janus
// doctest: doctest name=stdlib-std-array-builder
import std.array_builder
def main() : int {
    val builder : ArrayBuilder[int] = new ArrayBuilder[int](usize(2))
    builder.add(10)
    builder.add(20)
    val values : Array[int] = builder.result()
    val valid : bool = values.size() == usize(2) && values.get(usize(1)) == 20
    delete values
    delete builder
    return if valid { 0 } else { 1 }
}
```

### `std.builder`

```janus
// doctest: doctest name=stdlib-std-builder
import std.builder
import std.array
import std.array_builder
def finish[B <: Builder[int, Array[int]]](builder : B) : Array[int] {
    defer delete builder
    builder.add(42)
    return builder.result()
}
def main() : int {
    val values : Array[int] = finish[ArrayBuilder[int]](
        new ArrayBuilder[int](usize(1))
    )
    val valid : bool = values.get(usize(0)) == 42
    delete values
    return if valid { 0 } else { 1 }
}
```

### `std.bytes`

`ByteView` permet d'observer une zone binaire sans copie avec des accès bornés.
Elle est fournie par une callback `scoped` afin de ne pas survivre au stockage
qui la contient.

```janus
// doctest: doctest name=stdlib-std-bytes
import std.bytes
import std.io
def main() : int {
    val buffer : ByteBuffer = new ByteBuffer(usize(2))
    buffer.appendByte(byte(7))
    val value : int = buffer.withView[int](
        (borrow view : ByteView) => int(view.get(usize(0)))
    )
    delete buffer
    return value - 7
}
```

### `std.iterator`

```janus
// doctest: doctest name=stdlib-std-iterator
import std.iterator
import std.range
def main() : int {
    val sum : int = range(1, 5)
        .map[int]((value : int) => value * 2)
        .fold[int](0, (total : int, value : int) => total + value)
    return sum - 20
}
```

### `std.slice`

```janus
// doctest: doctest name=stdlib-std-slice
import std.array
import std.slice
def main() : int {
    val values : Array[int] = new Array[int](usize(1))
    values.push(7)
    val view : Slice[int] = new Slice[int](values, usize(0), usize(1))
    val result : int = view.get(usize(0))
    delete view
    delete values
    return result - 7
}
```

### `std.option`

```janus
// doctest: doctest name=stdlib-std-option
import std.option
def main() : int {
    val answer : Option[int] = Option.Some[int](21)
    val doubled : Option[int] = mapBorrowed[int, int](
        answer,
        (borrow value : int) => value * 2
    )
    return match doubled { Some(value) => value - 42, None => 1 }
}
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
def main() : int {
    var sum : int = 0
    for value in range(2, 5) { sum = sum + value }
    return sum - 9
}
```

### `std.result`

```janus
// doctest: doctest name=stdlib-std-result
import std.result
def increment(value : Result[int, int]) : Result[int, int] {
    val number : int = value?
    return Result.Ok[int, int](number + 1)
}
def main() : int {
    return match increment(Result.Ok[int, int](41)) {
        Ok(value) => value - 42,
        Error(error) => 1
    }
}
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-result-requires-handling
import std.result
def main() : int {
    val failed : Result[int, int] = Result.Error[int, int](1)
    return failed
}
```

### `std.numeric`

```janus
// doctest: doctest name=stdlib-std-numeric
import std.numeric
import std.result
def main() : int {
    val converted : Result[ubyte, NumericCastError] = checkedCast[ubyte](255)
    return match converted { Ok(value) => 0, Error(error) => 1 }
}
```

La [matrice des conversions numériques](numeric-conversions.md) documente les
trois politiques et tous leurs cas limites.

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
def main() : int {
    val hashing : IntHashing = new IntHashing()
    val valid : bool = hashing.equals(7, 7) && !hashing.equals(7, 8)
    delete hashing
    return if valid { 0 } else { 1 }
}
```

### `std.hashmap`

```janus
// doctest: doctest name=stdlib-std-hashmap
import std.hashmap
import std.hashing
import std.option
def main() : int {
    val hashing : IntHashing = new IntHashing()
    val scores : HashMap[int, int, IntHashing] =
        new HashMap[int, int, IntHashing](usize(4), hashing)
    delete scores.put(7, 42)
    val valid : bool = match scores.getOption(7) {
        Some(value) => value == 42,
        None => false
    }
    delete scores
    delete hashing
    return if valid { 0 } else { 1 }
}
```

### `std.hashset`

```janus
// doctest: doctest name=stdlib-std-hashset
import std.hashset
import std.hashing
def main() : int {
    val hashing : IntHashing = new IntHashing()
    val ids : HashSet[int, IntHashing] =
        new HashSet[int, IntHashing](usize(4), hashing)
    val inserted : bool = ids.add(42)
    val valid : bool = inserted && ids.contains(42) && !ids.contains(7)
    delete ids
    delete hashing
    return if valid { 0 } else { 1 }
}
```

### `std.deque`

`Deque[T]` offre les deux extrémités d'un tampon circulaire propriétaire;
`Queue[T]` en expose une discipline FIFO plus étroite. Les deux collections
acceptent les types non `Copy` et peuvent être consommées par `for`.

```janus
// doctest: doctest name=stdlib-std-deque
import std.deque
def main() : int {
    val queue : Queue[int] = new Queue[int](usize(2))
    queue.enqueue(10)
    queue.enqueue(20)
    val first : int = queue.dequeue()
    delete queue
    return first - 10
}
```

### `std.error`

`AccessError` et `CapacityError` forment le vocabulaire partagé des variantes
non paniquantes de collections et de tampons.

```janus
// doctest: doctest name=stdlib-std-error
import std.array
import std.error
import std.result
def main() : int {
    val values : Array[int] = new Array[int](usize(0))
    val empty : bool = isError[int, AccessError](values.tryPop())
    delete values
    return if empty { 0 } else { 1 }
}
```

### `std.priority_queue`

La file utilise un tas binaire stocké dans un `Array`. Le comparateur place
la priorité la plus forte en premier et les valeurs équivalentes sortent en
FIFO. Les valeurs peuvent posséder des ressources et sont transférées par
`move` à l'insertion puis au retrait.

```janus
// doctest: doctest name=stdlib-std-priority-queue
import std.priority_queue
def main() : int {
    val queue : PriorityQueue[int] = new PriorityQueue[int](
        usize(4),
        (borrow left : int, borrow right : int) => left < right
    )
    queue.enqueue(3)
    queue.enqueue(1)
    val first : int = queue.dequeue()
    delete queue
    return first - 1
}
```

### `std.ordering`

`Ordering[T]` centralise une comparaison totale trichotomique. Les tableaux
peuvent réutiliser une stratégie avec `sortBy` au lieu de recréer une closure
pour chaque tri.

```janus
// doctest: doctest name=stdlib-std-ordering
import std.array
import std.ordering
def main() : int {
    val ordering : IntOrdering = new IntOrdering()
    val values : Array[int] = [3, 1, 2]
    values.sortBy[IntOrdering](ordering)
    val first : int = values.get(usize(0))
    delete values
    delete ordering
    return first - 1
}
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
def inspect(path : Path) : int {
    val valid : bool = path.componentCount() == usize(2)
    delete path
    return if valid { 0 } else { 1 }
}
def main() : int {
    return match move normalizePath("alpha/./beta") {
        Ok(path) => inspect(move path),
        Error(error) => 1
    }
}
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
def main() : int {
    val text : TextBuilder = new TextBuilder()
    text.append("answer=")
    text.appendInt(42)
    val valid : bool = text.view() == "answer=42" &&
        match parseInt("42") { Ok(value) => value == 42, Error(error) => false }
    delete text
    return if valid { 0 } else { 1 }
}
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
def main() : int {
    assertEqual[int](2 + 2, 4)
    val answer : Option[int] = Option.Some[int](42)
    assertSomeWhere[int](
        answer,
        (borrow value : int) => value == 42
    )
    delete answer
    return 0
}
```

```janus
// doctest: compile_fail=J0000 name=stdlib-std-testing-requires-import
def main() : int {
    assertTrue(unknownCondition)
    return 0
}
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
def main() : int {
    val first : Random = new Random(usize(1234))
    val second : Random = new Random(usize(1234))
    val valid : bool = first.nextUSize() == second.nextUSize() &&
        first.nextBounded(usize(6)) < usize(6)
    delete first
    delete second
    return if valid { 0 } else { 1 }
}
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
def main() : int {
    val now : WallTime = wallNow()
    return if now.unixMilliseconds() <= now.unixNanoseconds() { 0 } else { 1 }
}
```

## Graphisme et audio expérimentaux

### `std.graphics`

```janus
// doctest: doctest name=stdlib-std-graphics
import std.graphics
def main() : int {
    val point : Vector2 = vector2(12.0, 8.0)
    val tint : Color = rgba(ubyte(255), ubyte(128), ubyte(0), ubyte(255))
    return if point.x == 12.0 && tint != Black { 0 } else { 1 }
}
```

### `std.graphics.audio`

```janus
// doctest: doctest name=stdlib-std-graphics-audio
import std.graphics.audio
def main() : int {
    val loader : (string) => Sound = (file : string) => loadSound(file)
    delete loader
    return 0
}
```

### `std.graphics.drawing`

```janus
// doctest: doctest name=stdlib-std-graphics-drawing
import std.graphics.drawing
def main() : int {
    val camera : Camera2D = new Camera2D(0.0, 0.0, 4.0, 5.0, 0.0, 1.0)
    val valid : bool = camera.targetX == 4.0 && camera.zoom == 1.0
    delete camera
    return if valid { 0 } else { 1 }
}
```

### `std.graphics.input`

```janus
// doctest: doctest name=stdlib-std-graphics-input
import std.graphics.input
import std.graphics.types
def main() : int {
    val query : (Key) => bool = (key : Key) => isKeyPressed(key)
    delete query
    return if int(Key.Space) == 32 { 0 } else { 1 }
}
```

### `std.graphics.resources`

```janus
// doctest: doctest name=stdlib-std-graphics-resources
import std.graphics.resources
def main() : int {
    val loader : (string) => Image = (file : string) => loadImage(file)
    delete loader
    return 0
}
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
