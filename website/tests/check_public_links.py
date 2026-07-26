#!/usr/bin/env python3
"""Crawl a deployed static site and fail on broken same-origin links/assets."""

from __future__ import annotations

import argparse
from collections import deque
from html.parser import HTMLParser
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urldefrag, urljoin, urlsplit, urlunsplit
from urllib.request import Request, urlopen


class LinkParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.targets: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        attribute = "href" if tag in {"a", "link"} else "src" if tag in {"img", "script"} else None
        if attribute and values.get(attribute):
            self.targets.append(values[attribute] or "")


def canonical(url: str) -> str:
    clean, _ = urldefrag(url)
    parts = urlsplit(clean)
    return urlunsplit((parts.scheme, parts.netloc, parts.path, parts.query, ""))


def crawl(start: str) -> tuple[set[str], list[tuple[str, str, str]]]:
    start = canonical(start)
    origin = urlsplit(start).netloc
    pending: deque[tuple[str, str]] = deque([("point d’entrée", start)])
    visited: set[str] = set()
    broken: list[tuple[str, str, str]] = []

    while pending:
        source, url = pending.popleft()
        url = canonical(url)
        if url in visited:
            continue
        visited.add(url)

        try:
            request = Request(url, headers={"User-Agent": "Janus-site-link-checker/1.0"})
            with urlopen(request, timeout=20) as response:
                content_type = response.headers.get_content_type()
                body = response.read()
        except HTTPError as error:
            broken.append((source, url, f"HTTP {error.code}"))
            continue
        except (URLError, TimeoutError) as error:
            broken.append((source, url, str(error)))
            continue

        if content_type != "text/html":
            continue

        parser = LinkParser()
        parser.feed(body.decode("utf-8", errors="replace"))
        for raw_target in parser.targets:
            if not raw_target or raw_target.startswith(("mailto:", "tel:", "javascript:", "data:")):
                continue
            target = canonical(urljoin(url, raw_target))
            if urlsplit(target).netloc == origin and target not in visited:
                pending.append((url, target))

    return visited, broken


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("url", help="URL publique de départ")
    args = parser.parse_args()
    visited, broken = crawl(args.url)
    print(f"{len(visited)} ressources internes vérifiées")
    if broken:
        for source, target, error in broken:
            print(f"BROKEN {error}: {source} -> {target}")
        return 1
    print("Aucun lien interne cassé")
    return 0


if __name__ == "__main__":
    sys.exit(main())
