#!/usr/bin/env python3
"""check_workflow_uploads.py: no workflow uploads the game (docs/DECISIONS.md, "No workflow uploads the game").

Reads every workflow under .github/workflows and every composite action under .github/actions,
and fails on any upload step whose `path` could match a game binary or archive. The rule is that a workflow artifact of a public repository is
downloadable by anyone logged in to GitHub, so only text, frame images, saves and clock files
may be uploaded: never an executable, shared library, object, archive or other binary. For every
`actions/upload-artifact` step (and any other upload action listed in UPLOAD_ACTIONS), every
line of `with.path` must be a pattern that:

  * is not an exclusion (`!pattern`: a denylist);
  * has no brace (`{a,b}`: @actions/glob sets nobrace, so it matches nothing, and a pattern that
    reads as an allowlist must be one);
  * has no `${{ }}` expression (it cannot be checked);
  * names its files by an extension in its last path component: not a directory, not `*`, not
    `**`, not a name without an extension;
  * has an extension without a wildcard in it, and in ALLOWED_EXTENSIONS (text and logs, .ppm and .png frame images, .sav and .rtc saves and clocks).

It also fails: any action whose name (lowercased) says upload, release, publish, deploy, asset
or artifact and is not in KNOWN_ACTIONS; docker:// steps; external reusable workflows; and, in
`run:` text, gh release/api uploads, curl uploads, git push and similar (RUN_COMMANDS).

Exception: release.yml's archives (.tar.gz, .zip), in a step or a job whose `if` requires
`inputs.publish` (the condition is checked, not just searched for). A run without `publish`
then uploads nothing.

Usage: check_workflow_uploads.py [--root DIR] | --self-test
Exit 0 and no output when every workflow passes; otherwise one line per problem, exit 1.
Needs PyYAML (python3-yaml).
"""
import json
import os
import re
import sys
import tempfile

try:
    import yaml
except ImportError:
    sys.exit("check_workflow_uploads.py: needs PyYAML (apt install python3-yaml)")

WORKFLOW_DIR = os.path.join(".github", "workflows")
ACTION_DIR = os.path.join(".github", "actions")

# What the failure uploads really carry, by class. Text and logs: the smoke scripts' dumps and
# lists, the replays' scripts and diffs. Frame images: the replay and smoke gates write .ppm
# (.png is allowed too). Saves and clocks: the save gates' .sav and .rtc. None is a playable build.
TEXT_EXTENSIONS = (".log", ".txt", ".diff", ".replay", ".all", ".known-patterns")
FRAME_EXTENSIONS = (".ppm", ".png")
SAVE_EXTENSIONS = (".sav", ".rtc")
ALLOWED_EXTENSIONS = TEXT_EXTENSIONS + FRAME_EXTENSIONS + SAVE_EXTENSIONS

# Named for the message only: anything not in ALLOWED_EXTENSIONS fails.
BINARY_EXTENSIONS = (".exe", ".dll", ".so", ".dylib", ".gba", ".elf", ".o", ".obj", ".a", ".lib", ".bin",
                     ".zip", ".tar.gz", ".7z")

# Archives that release.yml may upload, with `publish`.
ARCHIVE_EXTENSIONS = (".tar.gz", ".zip")
ARCHIVE_WORKFLOW = "release.yml"

UPLOAD_ACTIONS = ("actions/upload-artifact", "actions/upload-pages-artifact")

# The forms of an `if` that mean "only with publish", split on && by conjunct.
PUBLISH_CONJUNCTS = ("inputs.publish", "inputs.publish == true", "inputs.publish == 'true'",
                     "github.event.inputs.publish == 'true'", "github.event.inputs.publish == true")


# The one upload of a directory: docs.yml is upstream's own workflow (mdBook output, the
# documentation site, no game), triggered on upstream's master only, a branch this repository
# does not have, so it does not run here. Exact workflow, action and path; nothing else.
DIRECTORY_EXCEPTIONS = (("docs.yml", "actions/upload-pages-artifact", "docs/book"),)


def is_publish_only(condition):
    """True if the `if:` condition $1 can only be true when inputs.publish is."""
    if condition is None:
        return False
    text = str(condition).strip()
    match = re.fullmatch(r"\$\{\{(.*)\}\}", text, re.S)
    if match:
        text = match.group(1).strip()
    if "||" in text or "!" in text.replace("!=", ""):
        return False
    return any(part.strip() in PUBLISH_CONJUNCTS for part in text.split("&&"))


def extension_of(pattern):
    """The extension of the last path component of $1 ('.tar.gz' for archives), or ''."""
    base = pattern.rsplit("/", 1)[-1]
    for double in (".tar.gz",):
        if base.endswith(double):
            return double
    dot = base.rfind(".")
    return base[dot:] if dot > 0 or (dot == 0 and len(base) > 1) else ""


def problems_of_pattern(pattern, archive_allowed):
    """The problems of one line of an upload path; empty if it passes."""
    if pattern.startswith("!"):
        return ["an exclusion (a denylist), not an allowlist of file patterns"]
    if "${{" in pattern:
        return ["has an expression, which cannot be checked"]
    if "{" in pattern or "}" in pattern:
        return ["has a brace; @actions/glob sets nobrace, so it matches nothing"]
    if pattern.endswith("/") or pattern.endswith("\\"):
        return ["is a directory"]
    ext = extension_of(pattern)
    if ext == "":
        return ["has no extension (a directory, or any file, could match)"]
    if re.search(r"[*?\[\]]", ext):
        return ["has a wildcard in its extension"]
    if ext in ALLOWED_EXTENSIONS:
        return []
    if ext in ARCHIVE_EXTENSIONS and archive_allowed:
        return []
    if ext in BINARY_EXTENSIONS:
        why = "a binary or archive extension"
        if ext in ARCHIVE_EXTENSIONS:
            why += " (allowed only in %s, in a job or step that needs inputs.publish)" % ARCHIVE_WORKFLOW
        return ["%s %s" % (ext, why)]
    return ["the extension %s is not on the allowed list (%s)" % (ext, " ".join(ALLOWED_EXTENSIONS))]


# Actions that are known not to upload (matched lowercased, without the @ref). Any other action
# whose name says upload, release, publish, deploy, asset or artifact fails.
KNOWN_ACTIONS = ("actions/checkout", "actions/cache", "actions/download-artifact",
                 "actions/attest-build-provenance", "actions/configure-pages",
                 "actions/deploy-pages", "vampire/setup-wsl", "mheap/github-action-required-labels",
                 "octokit/request-action")
UPLOADER_WORDS = re.compile(r"upload|release|publish|deploy|asset|artifact")

# Commands in `run:` text that send files somewhere (checked per logical line, continuations joined).
RUN_COMMANDS = (
    ("gh release create", re.compile(r"\bgh\s+release\s+create\b")),
    ("gh release", re.compile(r"\bgh\s+release\s+(upload|edit|delete-asset)\b")),
    ("gh api upload", re.compile(r"\bgh\s+api\b.*(uploads\.github\.com|/upload|--input|\s-F\s|--field|-X\s*(POST|PUT|PATCH)|--method\s+(POST|PUT|PATCH))")),
    ("curl upload", re.compile(r"\bcurl\b.*\s(-T|--upload-file|-F|--form|--data-binary|--data-raw|--data|-d)(\s|=|$)")),
    ("git push", re.compile(r"\bgit\s+(-\S+\s+)*push\b")),
    ("other upload", re.compile(r"\b(docker\s+push|npm\s+publish|twine\s+upload|scp|rsync\s.*:|aws\s+s3|gsutil|rclone)\b")),
)
# The one command that may appear: release.yml creating the release, in a publish-only step or job.
RELEASE_COMMAND = "gh release create"


def run_findings(text):
    """The names of the upload commands in the `run:` text $1."""
    joined = re.sub(r"\\\n", " ", text)
    return [label for line in joined.splitlines() for label, pattern in RUN_COMMANDS if pattern.search(line)]


def check_step(name, where, step, job, found):
    """Append the problems of one step (of a job or of a composite action) to $5."""
    uses = str(step.get("uses", "")).strip()
    low = uses.lower()
    base = low.split("@")[0]
    in_release = name == ARCHIVE_WORKFLOW
    publish_only = is_publish_only(step.get("if")) or (job is not None and is_publish_only(job.get("if")))
    if low.startswith("docker://"):
        found.append("%s: uses %s, a container nobody can check" % (where, uses))
    elif low and not low.startswith("./") and ".github/workflows/" in base:
        found.append("%s: uses %s, an external reusable workflow, which may upload" % (where, uses))
    elif base in UPLOAD_ACTIONS:
        path = (step.get("with") or {}).get("path")
        if path is None or not str(path).strip():
            found.append("%s: an upload with no path" % where)
        else:
            archive_allowed = in_release and publish_only
            action = base
            for line in str(path).splitlines():
                pattern = line.strip()
                if (name, action, pattern) in DIRECTORY_EXCEPTIONS or not pattern:
                    continue
                found.extend("%s: path %s %s" % (where, pattern, problem)
                             for problem in problems_of_pattern(pattern, archive_allowed))
    elif low and not low.startswith("./") and base not in KNOWN_ACTIONS and UPLOADER_WORDS.search(base):
        found.append("%s: uses %s, an upload, release, publish or deploy action that is not known "
                     "(add it to KNOWN_ACTIONS only after reading it)" % (where, uses))
    for label in run_findings(str(step.get("run", ""))):
        if label == RELEASE_COMMAND and in_release and publish_only:
            continue
        found.append("%s: run: %s (%s)" % (where, label,
                     "allowed only in %s, in a step or job that needs inputs.publish" % ARCHIVE_WORKFLOW
                     if label == RELEASE_COMMAND else "a command that sends files out"))


def check_workflow(name, text):
    """The problems of the workflow file $1 with the text $2, as lines."""
    found = []
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as error:
        return ["%s: not valid YAML: %s" % (name, str(error).splitlines()[0])]
    if not isinstance(data, dict) or not isinstance(data.get("jobs"), dict):
        return ["%s: no jobs" % name]
    for job_id, job in data["jobs"].items():
        job = job or {}
        if "uses" in job:
            check_step(name, "%s: job %s" % (name, job_id), {"uses": job["uses"], "if": job.get("if")}, None, found)
        for number, step in enumerate(job.get("steps") or [], 1):
            where = "%s: job %s, step %d (%s)" % (name, job_id, number, step.get("name", step.get("uses", "")))
            check_step(name, where, step, job, found)
    return found


def check_action(name, text):
    """The problems of the composite action file $2 (reported as $1): its steps, with no exception."""
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as error:
        return ["%s: not valid YAML: %s" % (name, str(error).splitlines()[0])]
    found = []
    steps = ((data or {}).get("runs") or {}).get("steps") or []
    for number, step in enumerate(steps, 1):
        check_step("action", "%s: step %d (%s)" % (name, number, step.get("name", step.get("uses", ""))), step, None, found)
    return found


def check_root(root):
    directory = os.path.join(root, WORKFLOW_DIR)
    if not os.path.isdir(directory):
        return ["%s: no such directory" % directory]
    found = []
    names = sorted(entry for entry in os.listdir(directory) if entry.endswith((".yml", ".yaml")))
    if not names:
        return ["%s: no workflows" % directory]
    for name in names:
        with open(os.path.join(directory, name), encoding="utf-8") as handle:
            found.extend(check_workflow(name, handle.read()))
    actions = os.path.join(root, ACTION_DIR)
    if os.path.isdir(actions):
        for entry in sorted(os.listdir(actions)):
            for leaf in ("action.yml", "action.yaml"):
                path = os.path.join(actions, entry, leaf)
                if os.path.isfile(path):
                    with open(path, encoding="utf-8") as handle:
                        found.extend(check_action(".github/actions/%s/%s" % (entry, leaf), handle.read()))
    return found


def workflow(paths, step_if=None, job_if=None, uses="actions/upload-artifact@v4"):
    """A workflow text with one upload step of the path lines $1."""
    body = "".join("            %s\n" % line for line in paths)
    return ("on: workflow_dispatch\njobs:\n  j:\n%s    runs-on: ubuntu-24.04\n    steps:\n"
            "      - name: up\n%s        uses: %s\n        with:\n          name: x\n          path: |\n%s"
            % ("    if: %s\n" % json.dumps(job_if) if job_if else "", "        if: %s\n" % json.dumps(step_if) if step_if else "", uses, body))


def self_test():
    failures = []

    def expect(label, name, text, wanted):
        """wanted: None for a pass, else a substring that some problem must contain."""
        got = check_workflow(name, text)
        if wanted is None and got:
            failures.append("%s: expected a pass, got %s" % (label, got))
        elif wanted is not None and not any(wanted in line for line in got):
            failures.append("%s: expected a problem with '%s', got %s" % (label, wanted, got))

    good = ["out/**/*.txt", "out/**/*.log", "out/**/*.diff", "out/**/*.replay", "out/**/*.all",
            "out/**/*.known-patterns", "platform/build/64-windows/test/*.log", "suite-linux64.log",
            "out/**/*.ppm", "out/**/*.png", "out/**/*.sav", "out/**/*.rtc"]
    for ext in TEXT_EXTENSIONS + FRAME_EXTENSIONS + SAVE_EXTENSIONS:
        expect("good " + ext, "native.yml", workflow(["out/**/*" + ext], step_if="failure()"), None)
    expect("good text patterns", "native.yml", workflow(good), None)
    expect("good, step if failure()", "native.yml", workflow(good, step_if="failure()"), None)
    for ext in (".exe", ".dll", ".so", ".dylib", ".gba", ".elf", ".o", ".obj", ".a", ".bin", ".7z"):
        expect("bad " + ext, "native.yml", workflow(["build/x" + ext]), ext)
        expect("bad " + ext + " in release.yml with publish", "release.yml",
               workflow(["dist/*" + ext], step_if="inputs.publish"), ext)
    for ext in (".zip", ".tar.gz"):
        expect("bad " + ext + " outside release.yml", "windows.yml", workflow(["dist/*" + ext], step_if="inputs.publish"), ext)
        expect("bad " + ext + " in release.yml, no condition", "release.yml", workflow(["dist/*" + ext]), ext)
        expect("bad " + ext + " in release.yml, not publish", "release.yml",
               workflow(["dist/*" + ext], step_if="!inputs.publish"), ext)
        expect("bad " + ext + " in release.yml, publish or", "release.yml",
               workflow(["dist/*" + ext], step_if="inputs.publish || always()"), ext)
        expect("bad " + ext + " in release.yml, publish == false", "release.yml",
               workflow(["dist/*" + ext], step_if="inputs.publish == false"), ext)
        expect("good " + ext + " in release.yml, step publish", "release.yml",
               workflow(["dist/*" + ext], step_if="inputs.publish"), None)
        expect("good " + ext + " in release.yml, job publish and always", "release.yml",
               workflow(["dist/*" + ext], job_if="${{ always() && inputs.publish }}"), None)
    expect("bad: a directory", "native.yml", workflow(["out/"]), "directory")
    expect("bad: a directory by **", "native.yml", workflow(["out/**"]), "no extension")
    expect("bad: a directory without a slash", "native.yml", workflow(["out"]), "no extension")
    expect("bad: no extension", "native.yml", workflow(["build/native64/pokeemerald-native"]), "no extension")
    expect("bad: a star", "native.yml", workflow(["*"]), "no extension")
    expect("bad: a wildcard extension", "native.yml", workflow(["out/*.*"]), "wildcard")
    expect("bad: a wildcard in the extension", "native.yml", workflow(["out/*.t*"]), "wildcard")
    expect("bad: another binary (.pdf)", "native.yml", workflow(["out/*.pdf"]), ".pdf")
    expect("bad: a core file", "native.yml", workflow(["out/core.1234"]), ".1234")
    expect("bad: a symbol map", "native.yml", workflow(["build/pokeemerald-native.symbols"]), ".symbols")
    expect("bad: a saves directory", "native.yml", workflow(["out/saves/"]), "directory")
    expect("bad: brace", "native.yml", workflow(["out/**/*.{txt,log}"]), "brace")
    expect("bad: exclusion", "native.yml", workflow(["out/**/*.txt", "!out/secret.txt"]), "denylist")
    expect("bad: expression", "native.yml", workflow(["out/${{ matrix.name }}.txt"]), "expression")
    expect("bad: one bad line among good ones", "native.yml", workflow(good + ["build/pokeemerald-native.exe"]), ".exe")
    expect("good: upstream's docs.yml pages upload", "docs.yml",
           workflow(["docs/book"], uses="actions/upload-pages-artifact@v3"), None)
    expect("bad: the same upload in another workflow", "native.yml",
           workflow(["docs/book"], uses="actions/upload-pages-artifact@v3"), "no extension")
    expect("bad: another directory in docs.yml", "docs.yml",
           workflow(["build"], uses="actions/upload-pages-artifact@v3"), "no extension")
    expect("bad: the exception's path with the artifact action", "docs.yml",
           workflow(["docs/book"]), "no extension")
    expect("bad: no path", "native.yml",
           "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - uses: actions/upload-artifact@v4\n        with:\n          name: x\n",
           "no path")
    expect("bad: not YAML", "native.yml", "jobs: [", "not valid YAML")
    expect("a job with no upload passes", "native.yml",
           "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - run: echo\n", None)
    def step_workflow(step, job_if=None):
        """A workflow text with the one step $1 (YAML lines after '- ')."""
        lines = step.split("\n")
        body = "      - " + lines[0] + "\n" + "".join("        %s\n" % line for line in lines[1:])
        return ("on: workflow_dispatch\njobs:\n  j:\n%s    runs-on: ubuntu-24.04\n    steps:\n%s"
                % ("    if: %s\n" % json.dumps(job_if) if job_if else "", body))

    for action in ("softprops/action-gh-release@v2", "ncipollo/release-action@v1", "actions/upload-release-asset@v1",
                   "someone/publish-everything@v1", "someone/deploy-it@v3"):
        expect("bad: unknown uploader " + action, "native.yml", step_workflow("uses: " + action), "action")
    expect("bad: container step", "native.yml", step_workflow("uses: docker://example/uploader:1"), "container")
    expect("bad: external reusable workflow (step)", "native.yml",
           step_workflow("uses: org/repo/.github/workflows/x.yml@main"), "reusable workflow")
    expect("bad: external reusable workflow (job)", "native.yml",
           "on: push\njobs:\n  j:\n    uses: org/repo/.github/workflows/x.yml@main\n", "reusable workflow")
    expect("good: a local reusable workflow", "native.yml",
           "on: push\njobs:\n  j:\n    uses: ./.github/workflows/x.yml\n", None)
    expect("bad: differently cased upload-artifact", "native.yml",
           step_workflow("uses: Actions/Upload-Artifact@v4\nwith:\n  path: build/game.exe"), ".exe")
    expect("good: differently cased upload-artifact of text", "native.yml",
           step_workflow("uses: Actions/Upload-Artifact@v4\nwith:\n  path: out/a.txt"), None)
    expect("good: known actions", "native.yml", step_workflow("uses: actions/checkout@v4"), None)
    for label, command in (("gh release upload", "gh release upload v1 dist/a.zip"),
                           ("gh release edit", "gh release edit v1 --notes x"),
                           ("gh api POST", "gh api -X POST repos/o/r/releases/1/assets"),
                           ("gh api uploads host", "gh api https://uploads.github.com/x --input a.zip"),
                           ("curl -T", "curl -T game.exe https://example.com/x"),
                           ("curl --upload-file", "curl --upload-file game.exe https://example.com/x"),
                           ("curl -F", "curl -F file=@game.exe https://example.com/x"),
                           ("curl --data-binary", "curl --data-binary @game.exe https://example.com/x"),
                           ("curl continued line", "curl --silent \\\n  -F file=@game.exe https://example.com/x"),
                           ("git push", "git push origin HEAD:refs/heads/x"),
                           ("docker push", "docker push example/game:1")):
        expect("bad: run: " + label, "native.yml", step_workflow("run: |\n  " + command.replace("\n", "\n  ")), "run:")
        expect("bad: run: " + label + " in release.yml without publish", "release.yml",
               step_workflow("run: |\n  " + command.replace("\n", "\n  ")), "run:")
    expect("good: run: curl download, gh api read and delete", "native.yml",
           step_workflow("run: |\n  curl -sSL https://example.com/x | tar -xz\n  gh api repos/o/r/actions/runs/1/artifacts\n  gh api -X DELETE repos/o/r/actions/artifacts/1"), None)
    release_create = "run: gh release create v1 dist/a.zip"
    expect("bad: gh release create outside release.yml", "native.yml", step_workflow(release_create, job_if="inputs.publish"), "gh release create")
    expect("bad: gh release create in release.yml, no condition", "release.yml", step_workflow(release_create), "gh release create")
    expect("bad: gh release create in release.yml, publish or always", "release.yml",
           step_workflow(release_create, job_if="inputs.publish && true || always()"), "gh release create")
    expect("good: gh release create in release.yml, publish job", "release.yml", step_workflow(release_create, job_if="inputs.publish"), None)
    expect("bad: archive, publish and true or always", "release.yml",
           workflow(["dist/*.zip"], step_if="inputs.publish && true || always()"), ".zip")
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, WORKFLOW_DIR))
        os.makedirs(os.path.join(root, ACTION_DIR, "up"))
        with open(os.path.join(root, WORKFLOW_DIR, "ok.yml"), "w", encoding="utf-8") as handle:
            handle.write(workflow(good))
        with open(os.path.join(root, ACTION_DIR, "up", "action.yml"), "w", encoding="utf-8") as handle:
            handle.write("name: up\nruns:\n  using: composite\n  steps:\n    - uses: actions/upload-artifact@v4\n"
                         "      with:\n        path: build/game.exe\n    - shell: bash\n      run: gh release upload v1 a.zip\n")
        found = check_root(root)
        if not (any("up/action.yml" in line and ".exe" in line for line in found)
                and any("up/action.yml" in line and "gh release" in line for line in found)):
            failures.append("check_root: a bad composite action was not reported: %s" % found)
    # The real tree's own files, in a scratch root, with one bad upload added, must fail as a whole.
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, WORKFLOW_DIR))
        for name, text in (("ok.yml", workflow(good)), ("bad.yml", workflow(["build/game.exe"]))):
            with open(os.path.join(root, WORKFLOW_DIR, name), "w", encoding="utf-8") as handle:
                handle.write(text)
        if not any("bad.yml" in line and ".exe" in line for line in check_root(root)):
            failures.append("check_root: the bad workflow in a directory was not reported")
    if failures:
        for line in failures:
            print("SELF-TEST FAILED:", line)
        return 1
    print("check_workflow_uploads.py self-test: ok")
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    root = "."
    if len(argv) == 3 and argv[1] == "--root":
        root = argv[2]
    elif len(argv) != 1:
        sys.exit("usage: check_workflow_uploads.py [--root DIR] | --self-test")
    found = check_root(root)
    for line in found:
        print(line)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
