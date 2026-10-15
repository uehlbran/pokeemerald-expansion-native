#!/usr/bin/env python3
"""check_workflow_uploads.py: no workflow uploads the game (docs/DECISIONS.md, "No workflow uploads the game").

Reads the structure of every workflow under .github/workflows and every composite action under
.github/actions, and fails on any upload step whose file list could match a game binary or archive.
The rule is that a workflow artifact of a public repository is downloadable by anyone logged in to
GitHub, so only text, frame images, saves and clock files may be uploaded: never an executable,
shared library, object, archive or other binary.

For every step that uses an upload action (UPLOAD_ACTIONS: the artifact and Pages uploads and the
release-asset actions, each with the input that lists its files), every line of that input must be a
pattern that:

  * is not an exclusion (`!pattern`: a denylist);
  * has no brace (`{a,b}`: @actions/glob sets nobrace, so it matches nothing, and a pattern that
    reads as an allowlist must be one);
  * has no `${{ }}` expression (it cannot be checked);
  * names its files by an extension in its last path component: not a directory, not `*`, not
    `**`, not a name without an extension;
  * has an extension without a wildcard in it, and in ALLOWED_EXTENSIONS (text and logs, .ppm and .png
    frame images, .sav and .rtc saves and clocks).

Every non-local `uses:` that is not an upload action must be on the allowlist ALLOWED_ACTIONS
(owner/repo, any subpath, any ref): the actions the tree uses today. An unknown action fails, so an
uploader that is not named here cannot pass unseen; adding an action means adding it to the list,
which is the review point. Every `uses: ./<path>` is resolved against the root and the action file it
names (action.yml or action.yaml) is checked like the rest; one that is missing fails. `docker://`
steps and external reusable workflows fail.

Exceptions, exact: release.yml's archives (.tar.gz, .zip), in a step or a job whose `if` requires
`inputs.publish` (the condition is checked, not just searched for), as the checker has always allowed
them; a run without `publish` then uploads nothing. And upstream's docs.yml upload of `docs/book`.

Not read: `run:` steps. What a shell command does is a matter for the reviewer of the workflow file,
as are the scripts a step calls and the caches (docs/DECISIONS.md, "No workflow uploads the game").

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

# The upload actions by lowercased owner/repo (any subpath), each with the `with:` input that lists
# the files it sends.
UPLOAD_ACTIONS = {
    "actions/upload-artifact": "path",
    "actions/upload-pages-artifact": "path",
    "actions/upload-release-asset": "asset_path",
    "softprops/action-gh-release": "files",
    "svenstaro/upload-release-action": "file",
}

# The non-local actions the tree uses besides UPLOAD_ACTIONS, by lowercased owner/repo: GitHub's own,
# Vampire/setup-wsl (windows.yml, pinned by commit), and the two that upstream's labels.yml and
# docs.yml use (inert here: master only). Any other `uses:` fails until it is added here after its
# code has been read.
ALLOWED_ACTIONS = ("actions/cache", "actions/checkout", "actions/download-artifact",
                   "actions/attest-build-provenance", "actions/configure-pages", "actions/deploy-pages",
                   "vampire/setup-wsl", "mheap/github-action-required-labels", "octokit/request-action")

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


class LocalActions:
    """The files a tree names as `uses: ./<path>`, each read once, with the root they are resolved against."""

    def __init__(self, root):
        self.root = os.path.realpath(root)
        self.seen = set()

    def relative(self, path):
        return os.path.relpath(path, self.root).replace(os.sep, "/")

    def check_file(self, path):
        """The problems of the composite action file $1 (real path), unless it was read already."""
        real = os.path.realpath(path)
        if real in self.seen:
            return []
        self.seen.add(real)
        with open(real, encoding="utf-8") as handle:
            return check_action(self.relative(real), handle.read(), self)

    def check(self, uses, where):
        """The problems of the local `uses: $1` at $2: the action it names, which must exist."""
        target = os.path.realpath(os.path.join(self.root, uses[2:]))
        if os.path.commonpath([self.root, target]) != self.root:
            return ["%s: uses %s, a path outside the repository" % (where, uses)]
        if os.path.isfile(target):
            if self.relative(os.path.dirname(target)) == WORKFLOW_DIR.replace(os.sep, "/"):
                return []  # a local reusable workflow, checked with the others
            return ["%s: uses %s, a file that is neither a workflow nor an action directory" % (where, uses)]
        for leaf in ("action.yml", "action.yaml"):
            if os.path.isfile(os.path.join(target, leaf)):
                return self.check_file(os.path.join(target, leaf))
        return ["%s: uses %s, a local action that is missing (no action.yml or action.yaml there)" % (where, uses)]


def check_step(name, where, step, job, found, local=None):
    """Append the problems of one step (of a job or of a composite action) to $5; $6 resolves local actions."""
    uses = str(step.get("uses", "")).strip()
    low = uses.lower()
    base = low.split("@")[0]
    owner_repo = "/".join(base.split("/")[:2])
    publish_only = is_publish_only(step.get("if")) or (job is not None and is_publish_only(job.get("if")))
    if low.startswith("docker://"):
        found.append("%s: uses %s, a container nobody can check" % (where, uses))
    elif low.startswith("./"):
        if local is not None:
            found.extend(local.check(uses, where))
    elif low and ".github/workflows/" in base:
        found.append("%s: uses %s, an external reusable workflow, which may upload" % (where, uses))
    elif owner_repo in UPLOAD_ACTIONS:
        files = (step.get("with") or {}).get(UPLOAD_ACTIONS[owner_repo])
        if files is None or not str(files).strip():
            found.append("%s: an upload with no %s" % (where, UPLOAD_ACTIONS[owner_repo]))
        else:
            archive_allowed = name == ARCHIVE_WORKFLOW and publish_only
            for line in str(files).splitlines():
                pattern = line.strip()
                if (name, base, pattern) in DIRECTORY_EXCEPTIONS or not pattern:
                    continue
                found.extend("%s: path %s %s" % (where, pattern, problem)
                             for problem in problems_of_pattern(pattern, archive_allowed))
    elif low and owner_repo not in ALLOWED_ACTIONS:
        found.append("%s: uses %s, an action that is not on the allowlist "
                     "(add it to ALLOWED_ACTIONS only after reading it)" % (where, uses))


def check_workflow(name, text, local=None):
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
            check_step(name, "%s: job %s" % (name, job_id), {"uses": job["uses"], "if": job.get("if")}, None, found,
                       local)
        for number, step in enumerate(job.get("steps") or [], 1):
            where = "%s: job %s, step %d (%s)" % (name, job_id, number, step.get("name", step.get("uses", "")))
            check_step(name, where, step, job, found, local)
    return found


def check_action(name, text, local=None):
    """The problems of the composite action file $2 (reported as $1): its steps, with no exception."""
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as error:
        return ["%s: not valid YAML: %s" % (name, str(error).splitlines()[0])]
    found = []
    steps = ((data or {}).get("runs") or {}).get("steps") or []
    for number, step in enumerate(steps, 1):
        check_step("action", "%s: step %d (%s)" % (name, number, step.get("name", step.get("uses", ""))), step, None,
                   found, local)
    return found


def check_root(root):
    directory = os.path.join(root, WORKFLOW_DIR)
    if not os.path.isdir(directory):
        return ["%s: no such directory" % directory]
    found = []
    local = LocalActions(root)
    names = sorted(entry for entry in os.listdir(directory) if entry.endswith((".yml", ".yaml")))
    if not names:
        return ["%s: no workflows" % directory]
    for name in names:
        with open(os.path.join(directory, name), encoding="utf-8") as handle:
            found.extend(check_workflow(name, handle.read(), local))
    # The actions under .github/actions are read whether or not a workflow uses them.
    actions = os.path.join(root, ACTION_DIR)
    if os.path.isdir(actions):
        for entry in sorted(os.listdir(actions)):
            for leaf in ("action.yml", "action.yaml"):
                path = os.path.join(actions, entry, leaf)
                if os.path.isfile(path):
                    found.extend(local.check_file(path))
    return found


def workflow(paths, step_if=None, job_if=None, uses="actions/upload-artifact@v4", key="path"):
    """A workflow text with one upload step whose input $5 lists the file patterns $1."""
    body = "".join("            %s\n" % line for line in paths)
    job_condition = "    if: %s\n" % json.dumps(job_if) if job_if else ""
    step_condition = "        if: %s\n" % json.dumps(step_if) if step_if else ""
    return ("on: workflow_dispatch\njobs:\n  j:\n%s    runs-on: example-runner\n    steps:\n"
            "      - name: up\n%s        uses: %s\n        with:\n          name: x\n          %s: |\n%s"
            % (job_condition, step_condition, uses, key, body))


def step_workflow(step, job_if=None):
    """A workflow text with the one step $1 (YAML lines after '- ')."""
    lines = step.split("\n")
    body = "      - " + lines[0] + "\n" + "".join("        %s\n" % line for line in lines[1:])
    job_condition = "    if: %s\n" % json.dumps(job_if) if job_if else ""
    return "on: workflow_dispatch\njobs:\n  j:\n%s    runs-on: example-runner\n    steps:\n%s" % (job_condition, body)


def self_test():
    failures = []

    def expect(label, name, text, wanted):
        """wanted: None for a pass, else a substring that some problem must contain."""
        got = check_workflow(name, text)
        if wanted is None and got:
            failures.append("%s: expected a pass, got %s" % (label, got))
        elif wanted is not None and not any(wanted in line for line in got):
            failures.append("%s: expected a problem with '%s', got %s" % (label, wanted, got))

    def expect_tree(label, files, wanted):
        """files: path -> text, written under a scratch root and checked as a whole; wanted as in expect()."""
        with tempfile.TemporaryDirectory() as root:
            for path, text in files.items():
                os.makedirs(os.path.dirname(os.path.join(root, path)), exist_ok=True)
                with open(os.path.join(root, path), "w", encoding="utf-8") as handle:
                    handle.write(text)
            got = check_root(root)
        if wanted is None and got:
            failures.append("%s: expected a pass, got %s" % (label, got))
        elif wanted is not None and not any(wanted in line for line in got):
            failures.append("%s: expected a problem with '%s', got %s" % (label, wanted, got))

    # The path rules, on the artifact upload.
    good = ["out/**/*.txt", "out/**/*.log", "out/**/*.diff", "out/**/*.replay", "out/**/*.all",
            "out/**/*.known-patterns", "example/dir/*.log", "example.log", "out/**/*.ppm", "out/**/*.png",
            "out/**/*.sav", "out/**/*.rtc"]
    for ext in TEXT_EXTENSIONS + FRAME_EXTENSIONS + SAVE_EXTENSIONS:
        expect("good " + ext, "native.yml", workflow(["out/**/*" + ext], step_if="failure()"), None)
    expect("good text patterns", "native.yml", workflow(good), None)
    expect("good: a blank line among the patterns", "native.yml", workflow(["out/a.txt", "", "out/b.log"]), None)
    for ext in (".exe", ".dll", ".so", ".dylib", ".gba", ".elf", ".o", ".obj", ".a", ".bin", ".7z"):
        expect("bad " + ext, "native.yml", workflow(["out/example" + ext]), ext)
        expect("bad " + ext + " in release.yml with publish", "release.yml",
               workflow(["out/*" + ext], step_if="inputs.publish"), ext)
    for ext in (".zip", ".tar.gz"):
        expect("bad " + ext + " outside release.yml", "windows.yml",
               workflow(["out/*" + ext], step_if="inputs.publish"), ext)
        expect("bad " + ext + " in release.yml, no condition", "release.yml", workflow(["out/*" + ext]), ext)
        expect("bad " + ext + " in release.yml, not publish", "release.yml",
               workflow(["out/*" + ext], step_if="!inputs.publish"), ext)
        expect("bad " + ext + " in release.yml, publish or", "release.yml",
               workflow(["out/*" + ext], step_if="inputs.publish || always()"), ext)
        expect("bad " + ext + " in release.yml, publish == false", "release.yml",
               workflow(["out/*" + ext], step_if="inputs.publish == false"), ext)
        expect("bad " + ext + " in release.yml, publish and true or always", "release.yml",
               workflow(["out/*" + ext], step_if="inputs.publish && true || always()"), ext)
        expect("good " + ext + " in release.yml, step publish", "release.yml",
               workflow(["out/*" + ext], step_if="inputs.publish"), None)
        expect("good " + ext + " in release.yml, job publish and always", "release.yml",
               workflow(["out/*" + ext], job_if="${{ always() && inputs.publish }}"), None)
    expect("bad: a directory", "native.yml", workflow(["out/"]), "directory")
    expect("bad: a directory by **", "native.yml", workflow(["out/**"]), "no extension")
    expect("bad: a directory without a slash", "native.yml", workflow(["out"]), "no extension")
    expect("bad: no extension", "native.yml", workflow(["out/example"]), "no extension")
    expect("bad: a star", "native.yml", workflow(["*"]), "no extension")
    expect("bad: a wildcard extension", "native.yml", workflow(["out/*.*"]), "wildcard")
    expect("bad: a wildcard in the extension", "native.yml", workflow(["out/*.t*"]), "wildcard")
    expect("bad: another binary (.pdf)", "native.yml", workflow(["out/*.pdf"]), ".pdf")
    expect("bad: a core file", "native.yml", workflow(["out/core.1234"]), ".1234")
    expect("bad: a symbol map", "native.yml", workflow(["out/example.symbols"]), ".symbols")
    expect("bad: brace", "native.yml", workflow(["out/**/*.{txt,log}"]), "brace")
    expect("bad: exclusion", "native.yml", workflow(["out/**/*.txt", "!out/example.txt"]), "denylist")
    expect("bad: expression", "native.yml", workflow(["out/${{ matrix.name }}.txt"]), "expression")
    expect("bad: one bad line among good ones", "native.yml", workflow(good + ["out/example.exe"]), ".exe")
    expect("bad: no path", "native.yml",
           step_workflow("uses: actions/upload-artifact@v4\nwith:\n  name: x"), "no path")
    # The upload actions: each with its own input.
    uploads = {"actions/upload-artifact": "path", "actions/upload-pages-artifact": "path",
               "actions/upload-release-asset": "asset_path", "softprops/action-gh-release": "files",
               "svenstaro/upload-release-action": "file"}
    if UPLOAD_ACTIONS != uploads:
        failures.append("UPLOAD_ACTIONS is %s, the self-test pins %s: change both, after reading the action"
                        % (UPLOAD_ACTIONS, uploads))
    for action, key in uploads.items():
        expect("good: %s of text" % action, "native.yml", workflow(["out/a.txt"], uses=action + "@v1", key=key), None)
        expect("bad: %s of a binary" % action, "native.yml",
               workflow(["out/example.exe"], uses=action + "@v1", key=key), ".exe")
        expect("bad: %s with no %s" % (action, key), "native.yml",
               step_workflow("uses: %s@v1\nwith:\n  name: x" % action), "no " + key)
    expect("bad: a subpath of an upload action", "native.yml",
           workflow(["out/example.exe"], uses="actions/upload-artifact/merge@v4"), ".exe")
    expect("bad: differently cased upload action", "native.yml",
           workflow(["out/example.exe"], uses="Actions/Upload-Artifact@v4"), ".exe")
    expect("good: differently cased upload action of text", "native.yml",
           workflow(["out/a.txt"], uses="Actions/Upload-Artifact@v4"), None)
    expect("good: upstream's docs.yml pages upload", "docs.yml",
           workflow(["docs/book"], uses="actions/upload-pages-artifact@v3"), None)
    expect("bad: the same upload in another workflow", "native.yml",
           workflow(["docs/book"], uses="actions/upload-pages-artifact@v3"), "no extension")
    expect("bad: another directory in docs.yml", "docs.yml",
           workflow(["build"], uses="actions/upload-pages-artifact@v3"), "no extension")
    expect("bad: the exception's path with the artifact action", "docs.yml", workflow(["docs/book"]), "no extension")
    # The allowlist of other actions.
    allowed = ("actions/cache", "actions/checkout", "actions/download-artifact", "actions/attest-build-provenance",
               "actions/configure-pages", "actions/deploy-pages", "vampire/setup-wsl",
               "mheap/github-action-required-labels", "octokit/request-action")
    if ALLOWED_ACTIONS != allowed:
        failures.append("ALLOWED_ACTIONS is %s, the self-test pins %s: change both, after reading the action"
                        % (ALLOWED_ACTIONS, allowed))
    for action in allowed:
        expect("good: allowed action " + action, "native.yml", step_workflow("uses: %s@v1" % action), None)
    expect("good: a subpath of an allowed action is the allowed action", "native.yml",
           step_workflow("uses: actions/checkout/sub@v4"), None)
    expect("good: differently cased allowed action", "native.yml", step_workflow("uses: Actions/Checkout@v4"), None)
    for action in ("example-owner/example-action@v1", "example-owner/example-action/sub@v1",
                   "Example-Owner/Example-Action@v1", "actions-extra/checkout@v4", "actions/checkout-fork@v4",
                   "actions/unknown-example@v1"):
        expect("bad: action not on the allowlist " + action, "native.yml", step_workflow("uses: " + action),
               "allowlist")
    expect("bad: container step", "native.yml", step_workflow("uses: docker://example-owner/example:1"), "container")
    expect("bad: external reusable workflow (step)", "native.yml",
           step_workflow("uses: example-owner/example-repo/.github/workflows/x.yml@main"), "reusable workflow")
    expect("bad: external reusable workflow (job)", "native.yml",
           "on: push\njobs:\n  j:\n    uses: example-owner/example-repo/.github/workflows/x.yml@main\n",
           "reusable workflow")
    expect("good: a local reusable workflow, not read without a root", "native.yml",
           "on: push\njobs:\n  j:\n    uses: ./.github/workflows/x.yml\n", None)
    expect("bad: not YAML", "native.yml", "jobs: [", "not valid YAML")
    expect("bad: no jobs", "native.yml", "on: push\n", "no jobs")
    expect("good: a job with no upload", "native.yml",
           "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - name: n\n", None)
    # Local actions, resolved against a scratch root.
    caller = "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - uses: %s\n"
    action_of = "name: a\nruns:\n  using: composite\n  steps:\n    - %s\n"
    upload_step = "uses: actions/upload-artifact@v4\n      with:\n        path: out/example.exe"
    idle = "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - name: n\n"
    expect_tree("bad: a composite action outside .github/actions", {
        WORKFLOW_DIR + "/w.yml": caller % "./example/x", "example/x/action.yml": action_of % upload_step}, ".exe")
    expect_tree("bad: a composite action nested under .github/actions", {
        WORKFLOW_DIR + "/w.yml": caller % "./.github/actions/a/b",
        ACTION_DIR + "/a/b/action.yml": action_of % upload_step}, ".exe")
    expect_tree("bad: a composite action named action.yaml", {
        WORKFLOW_DIR + "/w.yml": caller % "./example/x", "example/x/action.yaml": action_of % upload_step}, ".exe")
    expect_tree("bad: a local action that does not exist", {WORKFLOW_DIR + "/w.yml": caller % "./example/missing"},
                "missing")
    expect_tree("bad: a job-level uses of a local action that is missing", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    uses: ./missing\n"}, "missing")
    expect_tree("bad: a local action that reaches outside the root",
                {WORKFLOW_DIR + "/w.yml": caller % "./../elsewhere"}, "outside")
    expect_tree("bad: an action that uses a bad local action", {
        WORKFLOW_DIR + "/w.yml": caller % "./example/x", "example/x/action.yml": action_of % "uses: ./example/y",
        "example/y/action.yml": action_of % upload_step}, ".exe")
    expect_tree("bad: an unknown action inside a local action", {
        WORKFLOW_DIR + "/w.yml": caller % "./example/x",
        "example/x/action.yml": action_of % "uses: example-owner/example-action@v1"}, "allowlist")
    expect_tree("good: a local action that uses itself ends", {
        WORKFLOW_DIR + "/w.yml": caller % "./example/x", "example/x/action.yml": action_of % "uses: ./example/x"}, None)
    expect_tree("good: a clean local action outside .github/actions", {
        WORKFLOW_DIR + "/w.yml": caller % "./example/x", "example/x/action.yml": action_of % "name: n"}, None)
    expect_tree("good: a local reusable workflow that exists", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    uses: ./.github/workflows/x.yml\n",
        WORKFLOW_DIR + "/x.yml": idle}, None)
    expect_tree("bad: a local file that is not a workflow", {
        WORKFLOW_DIR + "/w.yml": caller % "./file.txt", "file.txt": "x\n"},
        "neither a workflow nor an action directory")
    expect_tree("bad: a workflow-like file outside .github/workflows", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    uses: ./other/x.yml\n", "other/x.yml": idle},
        "neither a workflow nor an action directory")
    expect_tree("bad: an action.yaml under .github/actions that nothing uses", {
        WORKFLOW_DIR + "/w.yml": idle, ACTION_DIR + "/idle/action.yaml": action_of % upload_step}, ".exe")
    expect_tree("bad: an action.yml under .github/actions that nothing uses", {
        WORKFLOW_DIR + "/w.yml": idle, ACTION_DIR + "/idle/action.yml": action_of % upload_step}, ".exe")
    expect_tree("bad: a bad workflow in a directory with a good one", {
        WORKFLOW_DIR + "/ok.yml": workflow(good), WORKFLOW_DIR + "/bad.yml": workflow(["out/example.exe"])}, "bad.yml")
    expect_tree("good: a directory with good workflows only", {WORKFLOW_DIR + "/ok.yml": workflow(good)}, None)
    expect_tree("bad: a workflow named .yaml", {WORKFLOW_DIR + "/w.yaml": workflow(["out/example.exe"])}, ".exe")
    expect_tree("bad: an action that is not valid YAML", {
        WORKFLOW_DIR + "/w.yml": idle, ACTION_DIR + "/idle/action.yml": "runs: ["}, "not valid YAML")
    expect_tree("bad: an archive upload in an action needs no publish condition to be refused", {
        WORKFLOW_DIR + "/w.yml": idle,
        ACTION_DIR + "/idle/action.yml": action_of % "if: inputs.publish\n      uses: actions/upload-artifact@v4\n"
        "      with:\n        path: out/example.zip"}, ".zip")
    expect_tree("bad: no workflows", {WORKFLOW_DIR + "/readme.txt": "x\n"}, "no workflows")
    with tempfile.TemporaryDirectory() as scratch:  # a root reached through a symlink resolves like the real one
        real = os.path.join(scratch, "real")
        os.makedirs(os.path.join(real, WORKFLOW_DIR))
        os.makedirs(os.path.join(real, "example", "x"))
        with open(os.path.join(real, WORKFLOW_DIR, "w.yml"), "w", encoding="utf-8") as handle:
            handle.write(caller % "./example/x")
        with open(os.path.join(real, "example", "x", "action.yml"), "w", encoding="utf-8") as handle:
            handle.write(action_of % "name: n")
        os.symlink(real, os.path.join(scratch, "link"))
        if check_root(os.path.join(scratch, "link")):
            failures.append("a root reached through a symlink: expected a pass, got %s"
                            % check_root(os.path.join(scratch, "link")))
        os.makedirs(os.path.join(scratch, "elsewhere"))
        with open(os.path.join(scratch, "elsewhere", "action.yml"), "w", encoding="utf-8") as handle:
            handle.write(action_of % "name: n")
        os.symlink(os.path.join(scratch, "elsewhere"), os.path.join(real, "example", "out"))
        with open(os.path.join(real, WORKFLOW_DIR, "w2.yml"), "w", encoding="utf-8") as handle:
            handle.write(caller % "./example/out")
        if not any("outside" in line for line in check_root(real)):
            failures.append("a local action that is a symlink out of the root was not refused")
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
