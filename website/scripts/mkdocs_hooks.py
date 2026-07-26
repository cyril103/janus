"""Hooks MkDocs du site Janus."""

from pathlib import Path
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from sync_reference_docs import sync  # noqa: E402


def on_config(config):
    website = Path(config.config_file_path).resolve().parent
    sync(website.parent, website / "docs" / "reference" / "generated")
    return config
