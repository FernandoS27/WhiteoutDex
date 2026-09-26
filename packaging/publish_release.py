#!/usr/bin/env python3
"""Build the WhiteoutDex installer and publish it as a GitHub release.

What the in-Max updater (src/post_startup_scripts/WhiteoutDexAutoUpdater.ms)
needs from a release:

  * It reads GET /repos/<owner>/<repo>/releases/latest, which skips drafts
    and prereleases, so only a full release is ever offered.
  * It strips one leading "v" from tag_name and compares the rest, part by
    part, with the installed version.txt.
  * It downloads the first asset named WhiteoutDex_Setup*.exe (case matters).
    A release without one only gets the releases page opened.
  * It sends no token, so it sees nothing while the repository is private.

This script bumps version.txt when asked, builds the `installer` target, then
creates the release as a draft, uploads the installer and only then publishes
it, so the updater never finds a release whose installer is still uploading.

Authentication: GITHUB_TOKEN or GH_TOKEN when set, otherwise the github.com
credential Git Credential Manager already holds for `git push`. Creating a
release needs Contents: write (the repo scope, for a classic token).

Examples, from the repository root:
  python packaging/publish_release.py --dry-run
  python packaging/publish_release.py --bump patch
  python packaging/publish_release.py --skip-build --notes "Updater test"
  python packaging/publish_release.py --skip-build --replace   # redo a version

Testing the updater while the repository is private: publish to a public
scratch repository instead (it needs at least one commit to put the tag on),
  python packaging/publish_release.py --repo <owner>/<scratch> --skip-build
then, in the MAXScript Listener of a 3ds Max with an older version installed:
  ::WdxUpdater.githubUser = "<owner>"
  ::WdxUpdater.githubRepo = "<scratch>"
  ::WdxUpdater.check manual:true
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = REPO_ROOT / "version.txt"
API_ROOT = "https://api.github.com"
USER_AGENT = "WhiteoutDex-ReleaseScript"

# The asset name queryLatestRelease accepts. .NET's Regex is case-sensitive by
# default, so this is too: an installer named any other way uploads fine and
# is never offered to anyone.
ASSET_NAME_RE = re.compile(r'^WhiteoutDex_Setup[^"/]*\.exe$')
# Inno Setup puts the version into VersionInfoVersion, which takes at most four
# numeric parts; anything else fails the installer compile.
VERSION_RE = re.compile(r"^\d+(\.\d+){0,3}$")
GITHUB_URL_RE = re.compile(r"github\.com[:/]+([^/]+)/([^/]+?)(?:\.git)?/*$", re.IGNORECASE)


class ReleaseError(Exception):
    pass


def die(msg: str):
    raise ReleaseError(msg)


def info(msg: str = "") -> None:
    print(msg, flush=True)


def warn(msg: str) -> None:
    print(f"warning: {msg}", file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------
# Versions
# ---------------------------------------------------------------------------

def version_parts(v: str) -> list[int]:
    # Same reading as the updater's compareVersions: filterString drops empty
    # parts, and a part counts only its leading digits (0 when it has none).
    parts = []
    for p in v.split("."):
        if p:
            m = re.match(r"\d+", p)
            parts.append(int(m.group()) if m else 0)
    return parts


def is_newer(candidate: str, than: str) -> bool:
    a, b = version_parts(candidate), version_parts(than)
    n = max(len(a), len(b))
    return a + [0] * (n - len(a)) > b + [0] * (n - len(b))


def tag_to_version(tag: str) -> str:
    return tag[1:] if tag[:1] in ("v", "V") else tag


def bumped(version: str, part: str) -> str:
    nums = [int(x) for x in version.split(".")]
    nums += [0] * (3 - len(nums))
    i = ("major", "minor", "patch").index(part)
    nums[i] += 1
    nums[i + 1:] = [0] * (len(nums) - i - 1)
    return ".".join(map(str, nums))


# ---------------------------------------------------------------------------
# Git
# ---------------------------------------------------------------------------

def git(*args: str, check: bool = True) -> str:
    r = subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        die(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    # rstrip only: porcelain status lines start with a meaningful space.
    return r.stdout.rstrip()


def github_remotes() -> dict[str, tuple[str, str]]:
    remotes = {}
    for name in git("remote").split():
        m = GITHUB_URL_RE.search(git("remote", "get-url", name, check=False))
        if m:
            remotes[name] = (m.group(1), m.group(2))
    return remotes


def resolve_repo(args) -> tuple[str, str, bool]:
    """(owner, name, whether it is a remote of this checkout)."""
    remotes = github_remotes()
    if args.repo:
        m = re.fullmatch(r"([\w.-]+)/([\w.-]+)", args.repo)
        if not m:
            die("--repo takes owner/name")
        owner, name = m.groups()
        known = {(o.lower(), n.lower()) for o, n in remotes.values()}
        return owner, name, (owner.lower(), name.lower()) in known
    if args.remote:
        if args.remote not in remotes:
            die(f"'{args.remote}' is not a github.com remote of this checkout")
        return (*remotes[args.remote], True)
    distinct = sorted(set(remotes.values()))
    if not distinct:
        die("no github.com remote found; pass --repo owner/name")
    if len(distinct) > 1:
        listed = ", ".join(f"{r}={o}/{n}" for r, (o, n) in sorted(remotes.items()))
        die(f"several GitHub repositories among the remotes ({listed}); pick one with --remote or --repo")
    return (*distinct[0], True)


def get_token() -> tuple[str, str]:
    for var in ("GITHUB_TOKEN", "GH_TOKEN"):
        value = os.environ.get(var, "").strip()
        if value:
            return value, var
    # GIT_TERMINAL_PROMPT=0 stops git from falling back to a console prompt
    # nobody sees; Git Credential Manager may still show its sign-in window.
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    try:
        r = subprocess.run(["git", "credential", "fill"], cwd=REPO_ROOT, env=env,
                           input="protocol=https\nhost=github.com\n\n",
                           capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        die("timed out waiting for the git credential helper")
    for line in r.stdout.splitlines():
        if line.startswith("password="):
            return line[len("password="):], "git credential helper"
    die("no GitHub token: set GITHUB_TOKEN, or run `git fetch` once so "
        "Git Credential Manager stores a github.com sign-in")


# ---------------------------------------------------------------------------
# GitHub REST
# ---------------------------------------------------------------------------

def describe(payload) -> str:
    if isinstance(payload, dict):
        msg = payload.get("message", "")
        if payload.get("errors"):
            msg += " " + json.dumps(payload["errors"])
        return msg or json.dumps(payload)
    return str(payload)[:500]


class GitHub:
    def __init__(self, owner: str, name: str, token: str):
        self.base = f"{API_ROOT}/repos/{owner}/{name}"
        self.token = token

    def call(self, method: str, url: str, body=None, *, data=None, headers=None,
             timeout: float = 60, auth: bool = True):
        """(status, parsed JSON). HTTP errors come back as a status, not an exception."""
        if not url.startswith("https://"):
            url = self.base + url
        hdrs = {
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": USER_AGENT,
        }
        if auth:
            hdrs["Authorization"] = f"Bearer {self.token}"
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            hdrs["Content-Type"] = "application/json"
        hdrs.update(headers or {})
        req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                status, raw = r.status, r.read()
        except urllib.error.HTTPError as e:
            status, raw = e.code, e.read()
        except OSError as e:
            die(f"{method} {url}: {getattr(e, 'reason', e)}")
        try:
            payload = json.loads(raw) if raw else None
        except ValueError:
            payload = raw.decode("utf-8", "replace")
        return status, payload

    def expect(self, ok: tuple[int, ...], method: str, url: str, body=None, **kw):
        status, payload = self.call(method, url, body, **kw)
        if status not in ok:
            die(f"{method} {url} -> HTTP {status}: {describe(payload)}")
        return payload


def releases_with_tag(gh: GitHub, tag: str) -> list[dict]:
    # Listed rather than GET /releases/tags/<tag>, which never returns drafts:
    # a draft has no tag until it is published.
    found, page = [], 1
    while True:
        batch = gh.expect((200,), "GET", f"/releases?per_page=100&page={page}")
        found += [r for r in batch if r.get("tag_name") == tag]
        if len(batch) < 100:
            return found
        page += 1


def remote_tag_exists(gh: GitHub, tag: str) -> bool:
    status, payload = gh.call("GET", f"/git/ref/tags/{urllib.parse.quote(tag)}")
    if status == 404:
        return False
    if status != 200:
        die(f"looking up tag {tag} -> HTTP {status}: {describe(payload)}")
    return True


class _UploadProgress:
    """File wrapper http.client streams from, printing whole percents as it goes."""

    def __init__(self, f, total: int, label: str):
        self._f, self._total, self._label = f, total, label
        self._done, self._shown = 0, -1
        # Redrawn in place on a console; a redirected log gets a line a quarter.
        self._tty = sys.stderr.isatty()

    def read(self, n: int = -1) -> bytes:
        chunk = self._f.read(n if n and n > 0 else 1 << 20)
        self._done += len(chunk)
        pct = self._done * 100 // self._total if self._total else 100
        if chunk and pct != self._shown and (self._tty or pct % 25 == 0):
            self._shown = pct
            lead = "\r" if self._tty else ""
            end = "\n" if not self._tty or self._done >= self._total else ""
            sys.stderr.write(f"{lead}  uploading {self._label}: {pct:3d}% of "
                             f"{self._total / 1048576:.1f} MiB{end}")
            sys.stderr.flush()
        return chunk


def upload_asset(gh: GitHub, release: dict, path: Path) -> dict:
    url = (release["upload_url"].split("{", 1)[0]
           + "?name=" + urllib.parse.quote(path.name))
    size = path.stat().st_size
    with path.open("rb") as f:
        # An explicit Content-Length keeps urllib from switching to chunked
        # transfer encoding, which the uploads endpoint rejects.
        return gh.expect((201,), "POST", url,
                         data=_UploadProgress(f, size, path.name),
                         headers={"Content-Type": "application/octet-stream",
                                  "Content-Length": str(size)},
                         timeout=600)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def build_installer(build_dir: Path, config: str) -> None:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        die(f"{build_dir} is not a configured build tree; configure it first:\n"
            f'  cmake -S . -B build -G "Visual Studio 17 2022" -A x64')
    m = re.search(r"^ISCC:FILEPATH=(.*)$",
                  cache.read_text(encoding="utf-8", errors="replace"), re.M)
    if not m or m.group(1).endswith("NOTFOUND"):
        die(f"{build_dir} was configured without Inno Setup, so it has no installer "
            "target; install Inno Setup 6 or reconfigure with -DISCC=<path to ISCC.exe>")
    cmake = shutil.which("cmake")
    if not cmake:
        die("cmake is not on PATH")
    # A plain argument list, never a shell: Git Bash would rewrite MSBuild
    # switches into drive paths.
    cmd = [cmake, "--build", str(build_dir), "--config", config,
           "--target", "installer", "--parallel"]
    info("> " + subprocess.list2cmdline(cmd))
    r = subprocess.run(cmd, cwd=REPO_ROOT)
    if r.returncode != 0:
        die(f"installer build failed (exit {r.returncode})")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Build the WhiteoutDex installer and publish it as a GitHub release.",
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ver = p.add_mutually_exclusive_group()
    ver.add_argument("--bump", choices=("major", "minor", "patch"),
                     help="increment version.txt before building")
    ver.add_argument("--version", metavar="X.Y.Z",
                     help="release this version (written to version.txt)")
    p.add_argument("--skip-build", action="store_true",
                   help="upload the installer already in <build-dir>/installer")
    p.add_argument("--installer", metavar="EXE",
                   help="upload this installer instead (implies --skip-build)")
    p.add_argument("--build-dir", default="build", metavar="DIR",
                   help="CMake multi-version build tree (default: build)")
    p.add_argument("--config", default="Release", help="build configuration (default: Release)")
    p.add_argument("--repo", metavar="OWNER/NAME",
                   help="repository to publish to (default: this checkout's GitHub remote)")
    p.add_argument("--remote", help="git remote to take the repository from, when there are several")
    p.add_argument("--target", metavar="REF",
                   help="branch or commit to tag (default: HEAD, which must be pushed; "
                        "for a --repo that is not a remote here, its default branch)")
    p.add_argument("--title", help='release title (default: "WhiteoutDex v<version>")')
    notes = p.add_mutually_exclusive_group()
    notes.add_argument("--notes", help="release notes, Markdown (default: generated by GitHub)")
    notes.add_argument("--notes-file", metavar="FILE", help="read the release notes from a file")
    p.add_argument("--draft", action="store_true",
                   help="leave the release a draft (the updater does not see drafts)")
    p.add_argument("--prerelease", action="store_true",
                   help="mark it a prerelease (the updater does not see prereleases)")
    p.add_argument("--replace", action="store_true",
                   help="delete an existing release and tag of this version first")
    p.add_argument("--allow-older", action="store_true",
                   help="publish even when the version is not newer than the latest release")
    p.add_argument("--dry-run", action="store_true",
                   help="run every check and show what would happen, changing nothing")
    return p.parse_args(argv)


def run(args) -> int:
    owner, name, is_checkout = resolve_repo(args)
    token, token_source = get_token()
    gh = GitHub(owner, name, token)

    status, repo = gh.call("GET", "")
    if status != 200:
        die(f"cannot open {owner}/{name} (HTTP {status}: {describe(repo)}); "
            "the token needs access to this repository")
    if repo.get("permissions") and not repo["permissions"].get("push"):
        die(f"the token cannot write to {owner}/{name}")
    info(f"Repository:     {owner}/{name} ({'private' if repo.get('private') else 'public'}, "
         f"auth: {token_source})")

    # --- Version and installer --------------------------------------------
    current = VERSION_FILE.read_text(encoding="utf-8").strip()
    if args.version:
        version = tag_to_version(args.version.strip())
    elif args.bump:
        if not VERSION_RE.match(current):
            die(f"version.txt holds '{current}', which cannot be bumped")
        version = bumped(current, args.bump)
    else:
        version = current
    if not VERSION_RE.match(version):
        die(f"'{version}' is not a version Inno Setup accepts (1 to 4 numeric parts)")
    tag = f"v{version}"
    info(f"Version:        {version}" + (f" (version.txt: {current})" if version != current else ""))

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = REPO_ROOT / build_dir
    prebuilt = bool(args.skip_build or args.installer)
    installer = (Path(args.installer).resolve() if args.installer
                 else build_dir / "installer" / f"WhiteoutDex_Setup_v{version}.exe")
    if not ASSET_NAME_RE.match(installer.name):
        die(f"{installer.name} does not match WhiteoutDex_Setup*.exe, so the updater would never download it")
    if prebuilt and not installer.is_file():
        die(f"installer not found: {installer}")

    # --- What is already published ----------------------------------------
    existing = releases_with_tag(gh, tag)
    tag_exists = remote_tag_exists(gh, tag)
    if (existing or tag_exists) and not args.replace:
        what = f"{len(existing)} release(s)" if existing else "a tag without a release"
        die(f"{tag} already exists on {owner}/{name} ({what}); bump the version, "
            "or pass --replace to delete and recreate it")

    status, latest = gh.call("GET", "/releases/latest")
    if status == 200:
        latest_tag = latest["tag_name"]
        info(f"Latest release: {latest_tag}")
        visible = not (args.draft or args.prerelease)
        replacing_latest = args.replace and latest_tag == tag
        if visible and not replacing_latest and not is_newer(version, tag_to_version(latest_tag)):
            msg = (f"{version} is not newer than the latest release {latest_tag}; "
                   f"publishing it moves 'latest' back and hides {latest_tag} from the updater")
            if not args.allow_older:
                die(msg + " (--allow-older does it anyway)")
            warn(msg)
    elif status == 404:
        info("Latest release: none yet")
    else:
        die(f"GET /releases/latest -> HTTP {status}: {describe(latest)}")

    # --- Commit to tag -----------------------------------------------------
    if args.target:
        target = args.target
    elif is_checkout:
        target = git("rev-parse", "HEAD")
    else:
        target = repo["default_branch"]
    status, commit = gh.call("GET", f"/commits/{urllib.parse.quote(target, safe='')}")
    if status != 200:
        hint = "check --target" if args.target else "push it first, or pass --target <branch or sha>"
        die(f"{target} is not on {owner}/{name} (HTTP {status}); {hint}")
    target_sha = commit["sha"]
    info(f"Target commit:  {target_sha[:10]} {commit['commit']['message'].splitlines()[0]}")

    if is_checkout and not args.target:
        head_version = git("show", "HEAD:version.txt", check=False).strip()
        if head_version != version:
            warn(f"version.txt at HEAD says {head_version or '(missing)'}, so {tag} will tag "
                 "a commit that does not carry this version; commit the bump if the release is to stay")
        changed = [l for l in git("status", "--porcelain", "--untracked-files=no").splitlines()
                   if l[3:] != "version.txt"]
        if changed and not prebuilt:
            warn(f"{len(changed)} tracked file(s) have uncommitted changes; "
                 f"the installer will include them, {tag} will not")

    notes = args.notes
    if args.notes_file:
        notes = Path(args.notes_file).read_text(encoding="utf-8")
    kind = "draft" if args.draft else "prerelease" if args.prerelease else "latest release"

    if args.dry_run:
        info()
        if version != current:
            info(f"Would write version.txt: {current} -> {version}")
        if prebuilt:
            info(f"Would upload {installer}")
        else:
            info(f"Would build {installer} with the `installer` target")
        if existing or tag_exists:
            info(f"Would delete the existing {tag} release/tag")
        info(f"Would publish {tag} as the {kind}, tagging {target_sha[:10]}")
        info("Dry run: nothing was changed.")
        return 0

    # --- Build -------------------------------------------------------------
    if version != current:
        original = VERSION_FILE.read_bytes()
        eol = re.search(rb"\r?\n$", original)
        VERSION_FILE.write_bytes(version.encode("ascii") + (eol.group() if eol else b""))
        info(f"version.txt: {current} -> {version}")
    if not prebuilt:
        try:
            build_installer(build_dir, args.config)
        except BaseException:
            # Otherwise a rerun of the same --bump would skip a version.
            if version != current:
                VERSION_FILE.write_bytes(original)
                warn(f"version.txt restored to {current}")
            raise
        if not installer.is_file():
            die(f"the build succeeded but {installer} is not there")

    # --- Publish -----------------------------------------------------------
    if args.replace:
        for r in existing:
            gh.expect((204,), "DELETE", f"/releases/{r['id']}")
            info(f"Deleted release {r.get('name') or tag} (id {r['id']})")
        if tag_exists:
            gh.expect((204,), "DELETE", f"/git/refs/tags/{urllib.parse.quote(tag)}")
            info(f"Deleted tag {tag}")

    body = {
        "tag_name": tag,
        "target_commitish": target_sha,
        "name": args.title or f"WhiteoutDex v{version}",
        "draft": True,
        "prerelease": args.prerelease,
    }
    if notes:
        body["body"] = notes
    else:
        body["generate_release_notes"] = True
    release = gh.expect((201,), "POST", "/releases", body)
    info(f"Created draft release {release['id']}")

    finished = False
    try:
        asset = upload_asset(gh, release, installer)
        info(f"Uploaded {asset['name']} ({asset['size']:,} bytes)")
        if not args.draft:
            patch = {"draft": False}
            if not args.prerelease:
                patch["make_latest"] = "true"
            release = gh.expect((200,), "PATCH", f"/releases/{release['id']}", patch)
        finished = True
    finally:
        if not finished:
            # A draft has no tag yet, so deleting it leaves nothing behind.
            status, _ = gh.call("DELETE", f"/releases/{release['id']}")
            if status == 204:
                warn("the draft release was removed")
            else:
                warn(f"could not remove the draft {release['html_url']} (HTTP {status}); delete it by hand")

    info()
    info(f"Published {kind}: {release['html_url']}")
    if args.draft or args.prerelease:
        info("The updater will not see it: /releases/latest skips drafts and prereleases.")
        return 0

    # --- What the updater will see ------------------------------------------
    status, latest = gh.call("GET", "/releases/latest")
    if status == 200 and latest.get("tag_name") == tag:
        urls = [a["browser_download_url"] for a in latest.get("assets", [])
                if ASSET_NAME_RE.match(a["name"])]
        info(f"/releases/latest now reports {tag}; installer: {urls[0] if urls else 'NONE'}")
    else:
        found = latest.get("tag_name") if isinstance(latest, dict) else None
        warn(f"/releases/latest reports {found or 'nothing'} (HTTP {status}), not {tag}")
    status, _ = gh.call("GET", "/releases/latest", auth=False)
    if status == 200:
        info(f"Anonymous access works: installs older than {version} will be offered this update.")
    else:
        info(f"Anonymous access -> HTTP {status}: the updater sends no token, so it cannot "
             "see this release while the repository is private (see --help for a workaround).")
    return 0


def main(argv=None) -> int:
    # Commit subjects can hold characters the console code page lacks.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except AttributeError:
            pass
    args = parse_args(argv)
    try:
        return run(args)
    except ReleaseError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
