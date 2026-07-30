from __future__ import annotations

import argparse
import base64
import getpass
import hashlib
import hmac
import json
import pathlib
import sys
import tarfile

from .core import RegistryStore, canonical_json, token_digest


def _store(args: argparse.Namespace) -> RegistryStore:
    return RegistryStore(
        args.data,
        args.signing_key_file.read_bytes(),
        args.key_id,
    )


def _secret(prompt: str) -> str:
    if sys.stdin.isatty():
        return getpass.getpass(prompt)
    return sys.stdin.readline().rstrip("\r\n")


def provision(args: argparse.Namespace) -> int:
    token = _secret("Token: ")
    store = _store(args)
    store.provision_token(token, args.subject, args.scope)
    print(token_digest(token))
    return 0


def revoke(args: argparse.Namespace) -> int:
    if not _store(args).revoke_token(args.digest):
        raise SystemExit("token not found or already revoked")
    return 0


def backup(args: argparse.Namespace) -> int:
    print(_store(args).create_backup(args.output))
    return 0


def restore(args: argparse.Namespace) -> int:
    print(RegistryStore.restore_backup(args.archive, args.data))
    return 0


def verify_audit(args: argparse.Namespace) -> int:
    store = _store(args)
    previous = "0" * 64
    for event in store.audit_records():
        record = {
            "action": event["action"],
            "details": event["details"],
            "package": event["package"],
            "previousDigest": event["previousDigest"],
            "requestId": event["requestId"],
            "result": event["result"],
            "subject": event["subject"],
            "timestamp": event["timestamp"],
            "version": event["version"],
        }
        encoded = canonical_json(record)
        digest = hashlib.sha256(encoded).hexdigest()
        signature = base64.urlsafe_b64encode(
            hmac.new(store.signing_key, encoded, hashlib.sha256).digest()
        ).rstrip(b"=").decode()
        if (
            event["previousDigest"] != previous
            or event["digest"] != digest
            or event["keyId"] != store.key_id
            or not hmac.compare_digest(event["signature"], signature)
        ):
            raise SystemExit(f"audit verification failed at sequence {event['sequence']}")
        previous = digest
    print(f"verified {len(store.audit_records())} audit events")
    return 0


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description="Administer the Janus registry")
    commands = root.add_subparsers(dest="command", required=True)

    def common(command: argparse.ArgumentParser) -> None:
        command.add_argument("--data", type=pathlib.Path, required=True)
        command.add_argument("--signing-key-file", type=pathlib.Path, required=True)
        command.add_argument("--key-id", required=True)

    provision_parser = commands.add_parser(
        "provision-token", help="store only the hash of a token read from stdin"
    )
    common(provision_parser)
    provision_parser.add_argument("--subject", required=True)
    provision_parser.add_argument("--scope", action="append", required=True)
    provision_parser.set_defaults(handler=provision)

    revoke_parser = commands.add_parser("revoke-token")
    common(revoke_parser)
    revoke_parser.add_argument("--digest", required=True)
    revoke_parser.set_defaults(handler=revoke)

    backup_parser = commands.add_parser("backup")
    common(backup_parser)
    backup_parser.add_argument("--output", type=pathlib.Path, required=True)
    backup_parser.set_defaults(handler=backup)

    restore_parser = commands.add_parser("restore")
    restore_parser.add_argument("--archive", type=pathlib.Path, required=True)
    restore_parser.add_argument("--data", type=pathlib.Path, required=True)
    restore_parser.set_defaults(handler=restore)

    verify_parser = commands.add_parser("verify-audit")
    common(verify_parser)
    verify_parser.set_defaults(handler=verify_audit)
    return root


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.handler(args)
    except (
        OSError,
        RuntimeError,
        ValueError,
        json.JSONDecodeError,
        tarfile.TarError,
    ) as error:
        print(f"registry-admin: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
