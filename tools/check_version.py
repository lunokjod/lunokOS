#!/usr/bin/env python3
"""Refuses a commit that changes files without raising `kernel::kVersion`.

Law 10 of the README: every change bumps the version, in the same commit. This walks a revision
range (the commits of a push or a pull request), skips merge and empty commits, and fails with the
commit, the files and the two numbers when a commit did not raise the version (docs/80).

**And every folder that changed raises its own too** (0.186.0, the paper's owner: *«les apps i
llibreries també pugen de versió amb els canvis… tot ha de pujar de versió si és modificat»*): a
commit that touches `apps/<id>/` owes a raise of that app's `manifest.json`, and one that touches
`lib/<name>/` owes a raise of that library's `library.json`. The system's number is one of them and
not the only one — a page of the About reads the versions of the libraries, so a library that moved
without saying so is a page that lies (docs/40, docs/45, docs/80).

The check is strict: a commit that only touches a document or a tool still has to raise the
version, because that is what the law says and it is what the two commits of 0.88.2 did not do.
"""

import json
import os
import re
import subprocess
import sys

VERSION_FILE = "lib/kernel/include/kernel/version.h"
VERSION_RE = re.compile(r'kVersion\[\]\s*=\s*"([0-9]+)\.([0-9]+)\.([0-9]+)"')
# The manifest of an app and the one of a library, and where their `version` lives.
APP_MANIFEST = "manifest.json"
LIB_MANIFEST = "library.json"
NUMBER_RE = re.compile(r"^\s*([0-9]+)\.([0-9]+)\.([0-9]+)\s*$")

MAX_FILES = 12


def git(*args):
    return subprocess.check_output(
        ["git", "--no-pager"] + list(args), text=True, stderr=subprocess.DEVNULL
    ).strip()


def version_at(rev):
    try:
        text = git("show", "%s:%s" % (rev, VERSION_FILE))
    except subprocess.CalledProcessError:
        return None
    found = VERSION_RE.search(text)
    return tuple(int(part) for part in found.groups()) if found else None


def files_of(commit):
    output = git("diff-tree", "--no-commit-id", "--name-only", "-r", "--root", commit)
    return [line for line in output.splitlines() if line]


def subject_of(commit):
    return git("log", "-1", "--format=%s", commit)


def parent_of(commit):
    try:
        return git("rev-parse", "%s^" % commit)
    except subprocess.CalledProcessError:
        return None


def describe(version):
    return ".".join(str(part) for part in version) if version else "no version.h"


def folders_of(files):
    """The apps and the libraries a commit touches, with the manifest each one owes a raise to."""
    found = {}
    for name in files:
        parts = name.split("/")
        if len(parts) < 3:
            continue
        if parts[0] == "apps":
            found["apps/%s" % parts[1]] = "apps/%s/%s" % (parts[1], APP_MANIFEST)
        elif parts[0] == "lib":
            found["lib/%s" % parts[1]] = "lib/%s/%s" % (parts[1], LIB_MANIFEST)
    return sorted(found.items())


def manifest_version_at(rev, path):
    """The `version` of a manifest at a revision, or None when there is no manifest there."""
    try:
        text = git("show", "%s:%s" % (rev, path))
    except subprocess.CalledProcessError:
        return None
    try:
        data = json.loads(text)
    except ValueError:
        return None
    return data.get("version")


def number_of(text):
    """`x.y.z` as a tuple of numbers, or None when it is not one."""
    if not isinstance(text, str):
        return None
    found = NUMBER_RE.match(text)
    return tuple(int(part) for part in found.groups()) if found else None


def describe_folder(version):
    return version if isinstance(version, str) and version else "no version"


def main():
    if len(sys.argv) != 3:
        print("usage: check_version.py <base> <head>", file=sys.stderr)
        return 2
    base, head = sys.argv[1], sys.argv[2]
    try:
        os.chdir(git("rev-parse", "--show-toplevel"))
    except subprocess.CalledProcessError:
        print("check_version: not inside a git repository", file=sys.stderr)
        return 2

    commits = git("rev-list", "--reverse", "--no-merges", "%s..%s" % (base, head)).split()
    if not commits:
        print("check_version: nothing to check between %s and %s" % (base[:12], head[:12]))
        return 0

    failures = 0
    for commit in commits:
        files = files_of(commit)
        if not files:
            continue  # an empty commit changes nothing
        parent = parent_of(commit)
        if parent is None:
            continue  # the first commit of a history has no version to raise
        before = version_at(parent)
        after = version_at(commit)
        raised = before is None or after is None or after > before
        if not raised:
            failures += 1
            print(
                "check_version: %s did not raise kernel::kVersion (%s -> %s)"
                % (commit[:12], describe(before), describe(after))
            )
            print("  %s" % subject_of(commit))
            for name in files[:MAX_FILES]:
                print("  file: %s" % name)
            if len(files) > MAX_FILES:
                print("  ... and %d more" % (len(files) - MAX_FILES))
        # **And the folder of every app and of every library that moved raises its own** (0.186.0): the system's
        # number says the tree changed, and the manifest says which part of it did.
        for folder, manifest in folders_of(files):
            was = manifest_version_at(parent, manifest)
            now = manifest_version_at(commit, manifest)
            if was is None or now is None:
                continue  # an app or a library that is new here has nothing to raise
            was_number, now_number = number_of(was), number_of(now)
            if was_number is not None and now_number is not None and now_number > was_number:
                continue
            failures += 1
            print(
                "check_version: %s changed %s without raising its version (%s -> %s)"
                % (commit[:12], folder, describe_folder(was), describe_folder(now))
            )
            print("  %s" % subject_of(commit))
            print("  manifest: %s" % manifest)

    if failures:
        print(
            "check_version: %d thing(s) without a version bump; law 10 of the README says every "
            "change raises it in the same commit, and so does the manifest of every app and of "
            "every library that moved (docs/80)." % failures
        )
        return 1
    print("check_version: %d commit(s), every one raised the versions it had to" % len(commits))
    return 0


if __name__ == "__main__":
    sys.exit(main())
