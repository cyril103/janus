#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import Optional


PROGRAMS = ("janus", "janusc", "janusup", "janus-lsp")


def package(root: Path, identity: str) -> Path:
    result = root / f"package-{identity}"
    suffix = ".exe" if os.name == "nt" else ""
    (result / "bin").mkdir(parents=True)
    for program in PROGRAMS:
        (result / "bin" / f"{program}{suffix}").write_text(
            identity, encoding="utf-8"
        )
    (result / "identity").write_text(identity, encoding="utf-8")
    return result


def invoke(janusup: Path, home: Path, *arguments: str,
           crash: Optional[str] = None,
           pause: Optional[str] = None) -> subprocess.CompletedProcess:
    environment = os.environ.copy()
    environment["JANUSUP_HOME"] = str(home)
    if crash:
        environment["JANUSUP_TEST_CRASH"] = crash
    if pause:
        environment["JANUSUP_TEST_PAUSE"] = pause
    return subprocess.run(
        [str(janusup), *arguments], env=environment, text=True,
        capture_output=True, timeout=30
    )


def start(janusup: Path, home: Path, *arguments: str,
          pause: Optional[str] = None) -> subprocess.Popen:
    environment = os.environ.copy()
    environment["JANUSUP_HOME"] = str(home)
    if pause:
        environment["JANUSUP_TEST_PAUSE"] = pause
    return subprocess.Popen(
        [str(janusup), *arguments], env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )


def assert_selected(home: Path, name: str, identity: str) -> None:
    assert (home / "default").read_text(encoding="utf-8").strip() == name
    assert (home / "toolchains" / name / "identity").read_text(
        encoding="utf-8"
    ) == identity
    suffix = ".exe" if os.name == "nt" else ""
    for program in PROGRAMS:
        shim = home / "bin" / f"{program}{suffix}"
        assert shim.exists(), f"missing published program {shim}"
        if os.name == "nt":
            assert shim.read_text(encoding="utf-8") == identity
        else:
            assert os.readlink(shim).split("/")[2] == name


def wait_for(path: Path, process: subprocess.Popen) -> None:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process.poll() is not None:
            output, error = process.communicate()
            raise AssertionError(output + error)
        time.sleep(0.01)
    raise AssertionError(f"timed out waiting for {path}")


def release(path: Path) -> None:
    path.write_text("continue", encoding="utf-8")


def install_ok(janusup: Path, home: Path, source: Path, name: str) -> None:
    result = invoke(janusup, home, "install", str(source), name)
    assert result.returncode == 0, result.stdout + result.stderr


def test_adversarial_names(janusup: Path, root: Path) -> None:
    home = root / "adversarial-home"
    stable = package(root, "stable-v1")
    nested = package(root, "nested-v1")
    install_ok(janusup, home, stable, "stable")
    marker = home / ".janusup-test/pause-install-after-stage.ready"
    nested_process = start(
        janusup, home, "install", str(nested), "stable.new-x",
        pause="install-after-stage"
    )
    wait_for(marker, nested_process)
    stable_process = start(janusup, home, "install", str(stable), "stable")
    time.sleep(0.15)
    assert stable_process.poll() is None
    release(marker.with_suffix(".release"))
    nested_output = nested_process.communicate(timeout=30)
    stable_output = stable_process.communicate(timeout=30)
    assert nested_process.returncode == 0, nested_output
    assert stable_process.returncode != 0, stable_output
    # A transaction for stable must not discover, select, or delete state owned
    # by the valid nested name. Both old user-visible names remain supported.
    result = invoke(janusup, home, "install", str(stable), "stable")
    assert result.returncode != 0
    assert (home / "toolchains/stable.new-x/identity").read_text(
        encoding="utf-8"
    ) == "nested-v1"
    state_names = [entry.name for entry in (home / ".janusup-state/locks").iterdir()]
    assert len(state_names) >= 3
    assert not any("stable.new-x" in name for name in state_names), (
        "raw names are not an injective transaction/lock encoding"
    )


def test_same_name_processes(janusup: Path, root: Path) -> None:
    home = root / "same-home"
    first_source = package(root, "same-first")
    second_source = package(root, "same-second")
    marker = home / ".janusup-test/pause-install-after-stage.ready"
    first = start(janusup, home, "install", str(first_source), "same",
                  pause="install-after-stage")
    wait_for(marker, first)
    second = start(janusup, home, "install", str(second_source), "same")
    time.sleep(0.15)
    assert second.poll() is None, "same-name operation did not take the toolchain lock"
    release(marker.with_suffix(".release"))
    first_out, first_err = first.communicate(timeout=30)
    second_out, second_err = second.communicate(timeout=30)
    assert first.returncode == 0, first_out + first_err
    assert second.returncode != 0, second_out + second_err
    assert_selected(home, "same", "same-first")


def test_global_lock_and_concurrent_uninstall(janusup: Path, root: Path) -> None:
    home = root / "global-home"
    alpha = package(root, "alpha")
    beta = package(root, "beta")
    spare = package(root, "spare")
    install_ok(janusup, home, spare, "spare")
    marker = home / ".janusup-test/pause-install-after-stage.ready"
    first = start(janusup, home, "install", str(alpha), "alpha",
                  pause="install-after-stage")
    wait_for(marker, first)
    different = start(janusup, home, "install", str(beta), "beta")
    uninstall = start(janusup, home, "uninstall", "spare")
    time.sleep(0.15)
    assert different.poll() is None, "different-name activation bypassed global lock"
    assert uninstall.poll() is None, "uninstall bypassed global lock"
    release(marker.with_suffix(".release"))
    results = [first.communicate(timeout=30), different.communicate(timeout=30),
               uninstall.communicate(timeout=30)]
    assert all(process.returncode == 0 for process in (first, different, uninstall)), results
    assert not (home / "toolchains/spare").exists()
    selected = (home / "default").read_text(encoding="utf-8").strip()
    identity = (home / "toolchains" / selected / "identity").read_text(
        encoding="utf-8"
    )
    assert_selected(home, selected, identity)

    # Repeat against the exact same inactive name. The replacement owns both
    # locks first; uninstall must wait, re-check default under those locks, and
    # must not tear down a partially published generation.
    target = "beta" if selected == "alpha" else "alpha"
    target_source = beta if target == "beta" else alpha
    default_result = invoke(janusup, home, "default", selected)
    assert default_result.returncode == 0, default_result.stderr
    marker.unlink(missing_ok=True)
    marker.with_suffix(".release").unlink(missing_ok=True)
    replacement = start(
        janusup, home, "replace", str(target_source), target,
        pause="install-after-stage"
    )
    wait_for(marker, replacement)
    same_uninstall = start(janusup, home, "uninstall", target)
    time.sleep(0.15)
    assert same_uninstall.poll() is None
    release(marker.with_suffix(".release"))
    replacement_output = replacement.communicate(timeout=30)
    uninstall_output = same_uninstall.communicate(timeout=30)
    assert replacement.returncode == 0, replacement_output
    assert same_uninstall.returncode != 0, uninstall_output
    target_identity = (target_source / "identity").read_text(encoding="utf-8")
    assert_selected(home, target, target_identity)


def test_production_binary_has_no_hooks_or_replace(
    janusup: Path, production_janusup: Path, root: Path
) -> None:
    home = root / "production-home"
    first = package(root, "production-first")
    second = package(root, "production-second")
    result = invoke(
        production_janusup, home, "install", str(first), "production",
        crash="install-after-stage", pause="install-after-stage"
    )
    assert result.returncode == 0, result.stdout + result.stderr
    replacement = invoke(
        production_janusup, home, "replace", str(second), "production"
    )
    assert replacement.returncode != 0
    assert_selected(home, "production", "production-first")


def test_activation_preserves_unmanaged_bin_entries(janusup: Path, root: Path) -> None:
    home = root / "unmanaged-bin-home"
    first = package(root, "unmanaged-first")
    second = package(root, "unmanaged-second")
    install_ok(janusup, home, first, "first")
    script = home / "bin/user-script"
    script.write_text("user-owned", encoding="utf-8")
    nested = home / "bin/user-tools/config"
    nested.mkdir(parents=True)
    (nested / "settings.toml").write_text("owned = true\n", encoding="utf-8")
    install_ok(janusup, home, second, "second")
    assert script.read_text(encoding="utf-8") == "user-owned"
    assert (nested / "settings.toml").read_text(encoding="utf-8") == "owned = true\n"


def test_prejournal_orphan_cleanup(janusup: Path, root: Path) -> None:
    home = root / "orphan-home"
    source = package(root, "orphan")
    crashed = invoke(
        janusup, home, "install", str(source), "orphan",
        crash="install-after-transaction-reserved"
    )
    assert crashed.returncode != 0
    transactions = home / ".janusup-state/transactions"
    valid_orphans = list(transactions.iterdir())
    assert len(valid_orphans) == 1
    malformed = transactions / "leave-this-user-entry"
    malformed.mkdir()
    referenced = transactions / "111-222-333"
    referenced.mkdir()
    journal = home / ".janusup-state/journals/toolchain-6b656570"
    journal.write_text('111-222-333 staged "keep" 0\n', encoding="utf-8")
    install_ok(janusup, home, source, "orphan")
    assert not valid_orphans[0].exists()
    assert malformed.exists(), "malformed transaction names must never be removed"
    assert referenced.exists(), "a journal-referenced transaction was removed"


def test_corrupt_and_missing_transaction_recovery(janusup: Path, root: Path) -> None:
    corrupt_home = root / "corrupt-journal-home"
    source = package(root, "corrupt-stable")
    install_ok(janusup, corrupt_home, source, "stable")
    journal = corrupt_home / ".janusup-state/journals/activation"
    journal.write_text("truncated", encoding="utf-8")
    recovered = invoke(janusup, corrupt_home, "default", "stable")
    assert recovered.returncode == 0, recovered.stdout + recovered.stderr
    assert_selected(corrupt_home, "stable", "corrupt-stable")
    quarantine = corrupt_home / ".janusup-state/quarantine"
    assert any(quarantine.iterdir()), "corrupt journal was not quarantined"

    mismatch_home = root / "mismatched-activation-home"
    alpha = package(root, "mismatch-alpha")
    beta = package(root, "mismatch-beta")
    # Keep managed binaries byte-identical so recovery must validate the
    # published POSIX link targets, not merely their contents.
    suffix = ".exe" if os.name == "nt" else ""
    for program in PROGRAMS:
        (beta / "bin" / f"{program}{suffix}").write_text(
            "mismatch-alpha", encoding="utf-8"
        )
    install_ok(janusup, mismatch_home, alpha, "alpha")
    crashed = invoke(
        janusup, mismatch_home, "install", str(beta), "beta",
        crash="activate-after-bin-publish"
    )
    assert crashed.returncode != 0
    activation_journal = mismatch_home / ".janusup-state/journals/activation"
    activation_id = activation_journal.read_text(encoding="utf-8").split()[0]
    activation_transaction = (
        mismatch_home / ".janusup-state/transactions" / activation_id
    )
    for child in activation_transaction.rglob("*"):
        if child.is_file() or child.is_symlink():
            child.unlink()
    for child in sorted(activation_transaction.rglob("*"), reverse=True):
        if child.is_dir():
            child.rmdir()
    activation_transaction.rmdir()
    activation_journal.write_text("truncated", encoding="utf-8")
    ambiguous = invoke(janusup, mismatch_home, "default", "alpha")
    assert ambiguous.returncode != 0, (
        "mismatched default/bin state was incorrectly accepted after journal loss"
    )
    repaired = invoke(janusup, mismatch_home, "default", "alpha")
    assert repaired.returncode == 0, repaired.stdout + repaired.stderr
    assert_selected(mismatch_home, "alpha", "mismatch-alpha")

    missing_home = root / "missing-transaction-home"
    old = package(root, "missing-old")
    new = package(root, "missing-new")
    install_ok(janusup, missing_home, old, "stable")
    crashed = invoke(
        janusup, missing_home, "replace", str(new), "stable",
        crash="install-after-stage"
    )
    assert crashed.returncode != 0
    journals = missing_home / ".janusup-state/journals"
    transaction_id = next(
        path.read_text(encoding="utf-8").split()[0]
        for path in journals.iterdir() if path.name.startswith("toolchain-")
    )
    transaction = missing_home / ".janusup-state/transactions" / transaction_id
    for child in transaction.rglob("*"):
        if child.is_file() or child.is_symlink():
            child.unlink()
    for child in sorted(transaction.rglob("*"), reverse=True):
        if child.is_dir():
            child.rmdir()
    transaction.rmdir()
    recovered = invoke(janusup, missing_home, "default", "stable")
    assert recovered.returncode == 0, recovered.stdout + recovered.stderr
    assert_selected(missing_home, "stable", "missing-old")
    quarantine = missing_home / ".janusup-state/quarantine"
    assert any(quarantine.iterdir()), "missing transaction journal was not quarantined"


def test_crash_matrix_local_replace(janusup: Path, root: Path) -> None:
    points = (
        "install-after-stage", "install-after-backup", "install-after-publish",
        "activate-after-state", "activate-after-bin-backup",
        "activate-after-bin-publish", "activate-after-default-temp",
        "activate-after-default-publish", "install-after-commit",
    )
    for index, point in enumerate(points):
        home = root / f"replace-crash-home-{index}"
        old = package(root, f"replace-old-{index}")
        new = package(root, f"replace-new-{index}")
        install_ok(janusup, home, old, "crash")
        crashed = invoke(janusup, home, "replace", str(new), "crash", crash=point)
        assert crashed.returncode != 0
        recovered = invoke(janusup, home, "default", "crash")
        assert recovered.returncode == 0, recovered.stdout + recovered.stderr
        identity = (home / "toolchains/crash/identity").read_text(encoding="utf-8")
        assert identity in (f"replace-old-{index}", f"replace-new-{index}")
        assert_selected(home, "crash", identity)


def run(janusup: Path, production_janusup: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="janusup-transaction-") as temporary:
        root = Path(temporary)
        test_adversarial_names(janusup, root)
        test_same_name_processes(janusup, root)
        test_global_lock_and_concurrent_uninstall(janusup, root)
        test_production_binary_has_no_hooks_or_replace(janusup, production_janusup, root)
        test_activation_preserves_unmanaged_bin_entries(janusup, root)
        test_prejournal_orphan_cleanup(janusup, root)
        test_corrupt_and_missing_transaction_recovery(janusup, root)
        test_crash_matrix_local_replace(janusup, root)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janusup", type=Path, required=True)
    parser.add_argument("--production-janusup", type=Path, required=True)
    arguments = parser.parse_args()
    run(arguments.janusup.resolve(), arguments.production_janusup.resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
