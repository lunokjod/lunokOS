#!/usr/bin/env python3
"""Refuses a commit that changes files without raising `kernel::kVersion`.

Law 10 of the README: every change bumps the version, in the same commit. This walks a revision
range (the commits of a push or a pull request), skips merge and empty commits, and fails with the
commit, the files and the two numbers when a commit did not raise the version (docs/80).

The check is strict: a commit that only touches a document or a tool still has to raise the
version, because that is what the law says and it is what the two commits of 0.88.2 did not do.
"""

import os
import re
import subprocess
import sys

VERSION_FILE = "lib/kernel/include/kernel/version.h"
VERSION_RE = re.compile(r'kVersion\[\]\s*=\s*"([0-9]+)\.([0-9]+)\.([0-9]+)"')

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
        if before is not None and after is not None and after > before:
            continue
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

    if failures:
        print(
            "check_version: %d commit(s) without a version bump; law 10 of the README says every "
            "change raises it in the same commit (docs/80)." % failures
        )
        return 1
    print("check_version: %d commit(s), every one raised the version" % len(commits))
    return 0


if __name__ == "__main__":
    sys.exit(main())
