#!/usr/bin/env python3
"""Executable model used by the higher-kinded-types RFC.

This is deliberately not a Janus frontend experiment.  It isolates the two
questions that the RFC needs numbers for: kind checking constructor arguments
and forming stable monomorphization keys after partial type application.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass


@dataclass(frozen=True)
class Kind:
    arity: int

    def __str__(self) -> str:
        return " -> ".join(["*"] * (self.arity + 1))


@dataclass(frozen=True)
class TypeConstructor:
    name: str
    arity: int


@dataclass(frozen=True)
class ConstructorArgument:
    name: str | None = None

    @property
    def is_hole(self) -> bool:
        return self.name is None


HOLE = ConstructorArgument()


@dataclass(frozen=True)
class ConstructorReference:
    declaration: TypeConstructor
    arguments: tuple[ConstructorArgument, ...]

    @property
    def kind(self) -> Kind:
        return Kind(sum(argument.is_hole for argument in self.arguments))

    def canonical(self) -> str:
        if not self.arguments:
            return self.declaration.name
        arguments = ",".join(
            "_" if argument.is_hole else argument.name or ""
            for argument in self.arguments
        )
        return f"{self.declaration.name}[{arguments}]"

    def apply(self, *types: str) -> str:
        if len(types) != self.kind.arity:
            raise ValueError(
                f"cannot apply '{self.canonical()}' to {len(types)} type "
                f"argument(s); it expects {self.kind.arity}"
            )
        replacements = iter(types)
        arguments = [
            next(replacements) if argument.is_hole else argument.name or ""
            for argument in self.arguments
        ]
        return f"{self.declaration.name}[{','.join(arguments)}]"


def reference(
    declaration: TypeConstructor,
    *arguments: ConstructorArgument,
) -> ConstructorReference:
    if not arguments:
        arguments = (HOLE,) * declaration.arity
    if len(arguments) != declaration.arity:
        raise ValueError(
            f"'{declaration.name}' takes {declaration.arity} type argument(s), "
            f"got {len(arguments)}"
        )
    return ConstructorReference(declaration, tuple(arguments))


def require_kind(reference: ConstructorReference, expected: Kind) -> None:
    if reference.kind == expected:
        return
    suggestion = ""
    if reference.kind.arity > expected.arity:
        suggestion = (
            "; fix one additional parameter to a concrete type and keep '_' "
            "for the varying one, for example Result[_, E]"
        )
    raise TypeError(
        f"type constructor '{reference.canonical()}' has kind "
        f"{reference.kind}; expected {expected}{suggestion}"
    )


class MonomorphizationRegistry:
    def __init__(self) -> None:
        self.requests = 0
        self.keys: set[str] = set()

    def request(
        self,
        function: str,
        constructors: tuple[ConstructorReference, ...],
        concrete_types: tuple[str, ...],
    ) -> str:
        self.requests += 1
        constructor_part = ";".join(item.canonical() for item in constructors)
        type_part = ";".join(concrete_types)
        key = f"{function}__K[{constructor_part}]__T[{type_part}]"
        self.keys.add(key)
        return key


def run_scenario() -> dict[str, object]:
    unary = Kind(1)
    option = TypeConstructor("Option", 1)
    result = TypeConstructor("Result", 2)
    iterator = TypeConstructor("Iterator", 1)
    array = TypeConstructor("Array", 1)

    candidates = [
        reference(option),
        reference(iterator),
        reference(array),
        reference(result, HOLE, ConstructorArgument("IoError")),
    ]
    accepted: list[str] = []
    diagnostics: list[str] = []
    kind_checks = 0
    for candidate in candidates:
        kind_checks += 1
        require_kind(candidate, unary)
        accepted.append(candidate.canonical())

    invalid = [reference(result), reference(option, ConstructorArgument("int"))]
    for candidate in invalid:
        kind_checks += 1
        try:
            require_kind(candidate, unary)
        except TypeError as error:
            diagnostics.append(str(error))

    registry = MonomorphizationRegistry()
    option_ref, iterator_ref, array_ref, result_io_ref = candidates
    for constructor in (option_ref, iterator_ref, result_io_ref):
        registry.request("map", (constructor,), ("int", "string"))
    result_parse_ref = reference(
        result, HOLE, ConstructorArgument("ParseError")
    )
    registry.request(
        "traverse", (array_ref, option_ref), ("string", "int")
    )
    registry.request(
        "traverse", (array_ref, result_parse_ref), ("string", "int")
    )
    # Repeated requests demonstrate cache-key deduplication.
    registry.request("map", (option_ref,), ("int", "string"))
    registry.request(
        "traverse", (array_ref, option_ref), ("string", "int")
    )

    return {
        "accepted_constructors": accepted,
        "diagnostics": diagnostics,
        "kind_checks": kind_checks,
        "kind_model_nodes": 3,
        "monomorphization_requests": registry.requests,
        "unique_specializations": len(registry.keys),
        "specialization_keys": sorted(registry.keys),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true", help="emit JSON")
    arguments = parser.parse_args()
    report = run_scenario()
    if arguments.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            f"{report['kind_checks']} kind checks, "
            f"{report['unique_specializations']} unique specializations from "
            f"{report['monomorphization_requests']} requests"
        )
        for diagnostic in report["diagnostics"]:
            print(f"error: {diagnostic}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
