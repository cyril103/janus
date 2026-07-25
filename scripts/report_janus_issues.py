#!/usr/bin/env python3
"""List open Janus GitHub issues grouped by milestone and priority."""
from __future__ import annotations

import argparse
import json
import os
import urllib.error
import urllib.request
from collections import defaultdict
from typing import Any


PRIORITY_RANK = {
    "priority:high": 0,
    "priority:medium": 1,
    "priority:low": 2,
}


def get_token() -> str:
    token = os.getenv("GITHUB_TOKEN") or os.getenv("GH_TOKEN")
    if not token:
        raise SystemExit("No GitHub token found (GITHUB_TOKEN or GH_TOKEN).")
    return token


def api_request(token: str, endpoint: str) -> Any:
    request = urllib.request.Request(
        f"https://api.github.com{endpoint}",
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "User-Agent": "janus-issue-report",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(
            f"GitHub API returned {error.code} for {endpoint}: {detail}"
        ) from error


def fetch_open_issues(token: str, owner: str, repo: str) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    page = 1
    while True:
        batch = api_request(
            token,
            f"/repos/{owner}/{repo}/issues?state=open&per_page=100&page={page}",
        )
        if not batch:
            break
        issues.extend(issue for issue in batch if "pull_request" not in issue)
        if len(batch) < 100:
            break
        page += 1
    return issues


def priority(issue: dict[str, Any]) -> tuple[int, str]:
    labels = {label["name"] for label in issue.get("labels", [])}
    selected = next((name for name in PRIORITY_RANK if name in labels), "priority:unspecified")
    return PRIORITY_RANK.get(selected, 3), selected


def main() -> int:
    parser = argparse.ArgumentParser(
        description="List open issues grouped by milestone and sorted by priority."
    )
    parser.add_argument("--owner", default="cyril103")
    parser.add_argument("--repo", default="janus")
    args = parser.parse_args()

    issues = fetch_open_issues(get_token(), args.owner, args.repo)
    grouped: dict[tuple[int, str], list[dict[str, Any]]] = defaultdict(list)

    for issue in issues:
        milestone = issue.get("milestone")
        key = (
            milestone["number"] if milestone else 999_999,
            milestone["title"] if milestone else "Sans milestone",
        )
        grouped[key].append(issue)

    print(f"Issues ouvertes : {len(issues)} — {args.owner}/{args.repo}")
    if not issues:
        return 0

    for (_, milestone_title), milestone_issues in sorted(grouped.items()):
        print(f"\n## {milestone_title}")
        for issue in sorted(milestone_issues, key=lambda item: (priority(item)[0], item["number"])):
            _, priority_label = priority(issue)
            print(
                f"- #{issue['number']} [{priority_label}] {issue['title']}\n"
                f"  {issue['html_url']}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
