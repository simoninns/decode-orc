#!/usr/bin/env python3
#
# File:        validate_index.py
# Module:      orc-plugin-registry
# Purpose:     Validate a curated plugin index document
#
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 decode-orc contributors
#
# Offline checks (default): schema conformance, artifact naming (incl. ABI
# token), SPDX license present, and mandatory sha256 digest present/well-formed.
# With --online: additionally resolve each artifact URL and verify that the
# downloaded bytes hash to the declared sha256.
#
# Exit status is non-zero when any error is found; every error is printed.

import argparse
import hashlib
import re
import sys

import yaml

KNOWN_SCHEMA_MAJOR = 1
VALID_PLATFORMS = ("linux", "macos", "windows")
VALID_EXTENSIONS = (".so", ".dylib", ".dll")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
# orc-plugin_<stage>_<platform>[-<arch>][_abi<N>].<ext>
# The <stage> token may itself contain underscores (e.g. "skeleton_passthrough"),
# matching the host's authoritative parser segment class in
# orc/core/plugin_artifact_name.cpp.
ASSET_NAME_RE = re.compile(
    r"^orc-plugin_[A-Za-z0-9._\-]+_(linux|macos|windows)"
    r"(-[A-Za-z0-9_]+)?(_abi[0-9]+)?\.(so|dylib|dll)$"
)
# A permissive SPDX identifier shape (not the full SPDX grammar).
SPDX_RE = re.compile(r"^[A-Za-z0-9.\-+]+$")


def asset_name_from_url(url):
    name = url.rsplit("/", 1)[-1]
    return name.split("?", 1)[0].split("#", 1)[0]


def validate_artifact(errors, plugin_id, index, artifact):
    where = "plugin '%s' artifact[%d]" % (plugin_id, index)
    if not isinstance(artifact, dict):
        errors.append("%s: artifact is not a mapping" % where)
        return

    platform = artifact.get("platform", "")
    if not platform:
        errors.append("%s: missing 'platform'" % where)
    elif not any(platform == p or platform.startswith(p + "-")
                 for p in VALID_PLATFORMS):
        errors.append("%s: platform '%s' is not one of %s"
                      % (where, platform, ", ".join(VALID_PLATFORMS)))

    if "host_abi" not in artifact or not isinstance(artifact["host_abi"], int):
        errors.append("%s: 'host_abi' must be an integer" % where)

    url = artifact.get("url", "")
    if not url:
        errors.append("%s: missing 'url'" % where)

    sha = artifact.get("sha256", "")
    if not sha:
        errors.append("%s: missing mandatory 'sha256'" % where)
    elif not SHA256_RE.match(str(sha)):
        errors.append("%s: 'sha256' is not a 64-hex digest" % where)

    name = artifact.get("asset_name") or (asset_name_from_url(url) if url else "")
    if name:
        if not name.endswith(VALID_EXTENSIONS):
            errors.append("%s: artifact name '%s' has no plugin extension"
                          % (where, name))
        elif not ASSET_NAME_RE.match(name):
            errors.append(
                "%s: artifact name '%s' does not follow "
                "orc-plugin_<stage>_<platform>[_abi<N>].<ext>" % (where, name))


def validate_plugin(errors, index, plugin):
    where = "plugin[%d]" % index
    if not isinstance(plugin, dict):
        errors.append("%s: entry is not a mapping" % where)
        return

    plugin_id = plugin.get("id", "")
    if not plugin_id:
        errors.append("%s: missing 'id'" % where)
        plugin_id = "<unknown>"

    license_spdx = plugin.get("license_spdx", "")
    if not license_spdx:
        errors.append("plugin '%s': missing 'license_spdx'" % plugin_id)
    elif not SPDX_RE.match(str(license_spdx)):
        errors.append("plugin '%s': 'license_spdx' is not a valid SPDX id"
                      % plugin_id)

    artifacts = plugin.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        errors.append("plugin '%s': 'artifacts' must be a non-empty list"
                      % plugin_id)
        return
    for i, artifact in enumerate(artifacts):
        validate_artifact(errors, plugin_id, i, artifact)


def validate_document(doc):
    errors = []
    if not isinstance(doc, dict):
        return ["index root is not a mapping"]

    schema = doc.get("registry_schema")
    if not isinstance(schema, int):
        errors.append("'registry_schema' must be an integer")
    elif schema < 1 or schema > KNOWN_SCHEMA_MAJOR:
        errors.append("'registry_schema' %r is not a supported major version "
                      "(this validator understands %d)"
                      % (schema, KNOWN_SCHEMA_MAJOR))

    plugins = doc.get("plugins", [])
    if plugins is None:
        plugins = []
    if not isinstance(plugins, list):
        errors.append("'plugins' must be a list")
        return errors
    for i, plugin in enumerate(plugins):
        validate_plugin(errors, i, plugin)
    return errors


def verify_online(doc):
    import urllib.request

    errors = []
    for plugin in doc.get("plugins", []) or []:
        pid = plugin.get("id", "<unknown>")
        for i, artifact in enumerate(plugin.get("artifacts", []) or []):
            url = artifact.get("url", "")
            expected = str(artifact.get("sha256", "")).lower()
            if not url or not expected:
                continue
            try:
                with urllib.request.urlopen(url, timeout=60) as resp:
                    data = resp.read()
            except Exception as exc:  # noqa: BLE001 - report any transport error
                errors.append("plugin '%s' artifact[%d]: url unreachable (%s)"
                              % (pid, i, exc))
                continue
            actual = hashlib.sha256(data).hexdigest()
            if actual != expected:
                errors.append(
                    "plugin '%s' artifact[%d]: sha256 mismatch "
                    "(declared %s, downloaded %s)" % (pid, i, expected, actual))
    return errors


def main(argv):
    parser = argparse.ArgumentParser(description="Validate a plugin index")
    parser.add_argument("index", help="path to index.yaml")
    parser.add_argument("--online", action="store_true",
                        help="also verify artifact URLs and digests")
    args = parser.parse_args(argv)

    try:
        with open(args.index, "r", encoding="utf-8") as handle:
            doc = yaml.safe_load(handle)
    except (OSError, yaml.YAMLError) as exc:
        print("error: failed to load %s: %s" % (args.index, exc),
              file=sys.stderr)
        return 2

    errors = validate_document(doc)
    if not errors and args.online:
        errors += verify_online(doc)

    if errors:
        for message in errors:
            print("error: " + message, file=sys.stderr)
        print("FAILED: %d problem(s) in %s" % (len(errors), args.index),
              file=sys.stderr)
        return 1

    print("OK: %s is valid" % args.index)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
