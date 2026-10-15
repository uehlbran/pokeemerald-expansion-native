#!/usr/bin/env python3
"""check_workflow_uploads.py: no workflow uploads the game (docs/DECISIONS.md, "No workflow uploads the game").

Reads every workflow under .github/workflows and every composite action under .github/actions,
and fails on any upload step whose `path` could match a game binary or archive. The rule is that a
workflow artifact of a public repository is downloadable by anyone logged in to GitHub, so only text,
frame images, saves and clock files may be uploaded: never an executable, shared library, object,
archive or other binary. For every `actions/upload-artifact` step (and any other upload action listed in
UPLOAD_ACTIONS), every line of `with.path` must be a pattern that:

  * is not an exclusion (`!pattern`: a denylist);
  * has no brace (`{a,b}`: @actions/glob sets nobrace, so it matches nothing, and a pattern that
    reads as an allowlist must be one);
  * has no `${{ }}` expression (it cannot be checked);
  * names its files by an extension in its last path component: not a directory, not `*`, not
    `**`, not a name without an extension;
  * has an extension without a wildcard in it, and in ALLOWED_EXTENSIONS (text and logs, .ppm and .png
    frame images, .sav and .rtc saves and clocks).

Every non-local `uses:` must be an action on the allowlist ALLOWED_ACTIONS (owner/repo, any
subpath, any ref): the actions the tree uses today. Adding an action means adding it there, which
is the review point. Every `uses: ./<path>` is resolved against the root and the action file it
names (action.yml or action.yaml) is checked like the rest; one that is missing fails. Every `path:`
line of `actions/cache` (also its save and restore forms) must be one of ALLOWED_CACHE_PATHS, the exact
paths cached today: a cache may hold compiled objects, converted assets and ccache data, never a linked
game, test runner or archive of either, and a new cache path is added only after checking that. It also
fails docker:// steps; external reusable workflows; and `run:` text that uploads: each simple command is
read as words (command_findings), so gh release, gist and body-file commands, gh api writes (a field,
a body, or a method that is not GET, HEAD or DELETE), curl and wget uploads, rsync and scp to a remote,
git push and similar are found however their flags are spelled or quoted.

Not checked: what the scripts and make targets that a `run:` step calls do. Reading those stays a
review duty.

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
import shlex
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

# Matched by owner/repo (lowercased), so a subpath such as actions/upload-artifact/merge is checked too.
UPLOAD_ACTIONS = ("actions/upload-artifact", "actions/upload-pages-artifact")
CACHE_ACTIONS = ("actions/cache",)

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


# The non-local actions the tree uses besides UPLOAD_ACTIONS and CACHE_ACTIONS, by lowercased
# owner/repo: GitHub's own, Vampire/setup-wsl (windows.yml, pinned by commit), and the two that
# upstream's labels.yml and docs.yml use (inert here: master only). Any other `uses:` fails until it
# is added here after its code has been read.
ALLOWED_ACTIONS = ("actions/checkout", "actions/download-artifact", "actions/attest-build-provenance",
                   "actions/configure-pages", "actions/deploy-pages", "vampire/setup-wsl",
                   "mheap/github-action-required-labels", "octokit/request-action")

# The paths the tree caches today, exact strings of the `path:` lines of every actions/cache step: the
# host build tools (tools/<name>/<name>), ccache's directory, the converted assets and SDL2's MinGW
# release. None holds a linked game, test runner or archive. A cache path not listed here fails; adding
# one means checking that, then adding it here and to the self-test's copy of this list.
ALLOWED_CACHE_PATHS = (
    "tools/bin2c/bin2c", "tools/gbafix/gbafix", "tools/gbagfx/gbagfx", "tools/jsonproc/jsonproc",
    "tools/mapjson/mapjson", "tools/mid2agb/mid2agb", "tools/preproc/preproc", "tools/ramscrgen/ramscrgen",
    "tools/rsfont/rsfont", "tools/scaninc/scaninc", "tools/trainerproc/trainerproc",
    "tools/compresSmol/compresSmol", "tools/compresSmol/compresSmolTilemap", "tools/wav2agb/wav2agb",
    "tools/patchelf/patchelf", "tools/mgba-rom-test-hydra/mgba-rom-test-hydra",
    "~/.cache/ccache", "wsl-cache/ccache", "build/assets-native", "build/sdl2-mingw", "wsl-cache/sdl2-mingw",
)

# Commands in `run:` text that send files somewhere. A `run:` text is split into simple commands (at
# newlines, `&&`, `||`, `|`, `;` and `&`, continuations joined) and each command is read as words with
# the shell's quoting removed, so a flag is judged in the command that has it and however it is
# spelled. A command word inside a string (`echo "git push"`) is found too: the rule fails safe.
RELEASE_COMMAND = "gh release create"  # the one command that may appear: release.yml, publish-only

# A request method that sends nothing: reads, and the artifact cleanup's deletion. Any other method, a
# variable or an expression included, is taken for a write.
SAFE_METHODS = ("GET", "HEAD", "DELETE")

# gh api's flags that attach a field or a body (the request becomes a POST), by prefix.
GH_API_BODY_FLAGS = ("--field", "--raw-field", "--input")
# gh subcommands that take a file for a body or an asset.
GH_BODY_FILE_FLAGS = ("--body-file", "--notes-file")
GH_RELEASE_WRITES = ("upload", "edit", "delete-asset")
# git options that take their argument as the next word, before the subcommand.
GIT_OPTIONS_WITH_ARGUMENT = ("-C", "-c", "--git-dir", "--work-tree", "--namespace", "--super-prefix", "--config-env")
# curl's short options that take an argument, so letters after them are the argument, not more options
# (-T, -F and -d are the upload options and are looked for first).
CURL_SHORT_OPTIONS_WITH_ARGUMENT = "AbcCDeEHKmoPQrtuUwxXyYz"
CURL_UPLOAD_LONG_OPTIONS = ("--upload-file", "--form", "--json", "--data")
WGET_UPLOAD_LONG_OPTIONS = ("--post-", "--body-")
OTHER_UPLOAD_PROGRAMS = ("scp", "gsutil", "rclone")
SEPARATOR_CHARACTERS = set("&|;()")  # a word of only these ends the command: &&, ||, |, ;, &, |&, ;;


def simple_commands(text):
    """The simple commands of the `run:` text $1, each a list of words with quotes removed."""
    joined = re.sub(r"\\\n", " ", text)
    commands = []
    for line in joined.splitlines():
        try:
            lexer = shlex.shlex(line, posix=True, punctuation_chars=True)
            lexer.whitespace_split = True
            words = list(lexer)
        except ValueError:  # an unbalanced quote: a string that goes on in the next line
            words = [word for part in re.split(r"&&|\|\||[|;&()]", line) for word in part.split() + [";"]]
        command = []
        for word in words:
            if set(word) <= SEPARATOR_CHARACTERS:
                commands.append(command)
                command = []
            else:
                command.append(word)
        commands.append(command)
    return [command for command in commands if command]


def program_name(word):
    return os.path.basename(word.lstrip("`$("))


def request_method(words, index):
    """The method that the option at words[index] (-X, --method) names, or '' when it has none."""
    word = words[index]
    attached = word[2:] if not word.startswith("--") else word.partition("=")[2]
    if attached:
        return attached.lstrip("=")
    return words[index + 1] if index + 1 < len(words) and not word.endswith("=") else ""


def method_writes(words, short_option):
    """True if a -X/--method option of $1 names a method that is not in SAFE_METHODS ($2: '-X' or '')."""
    for index, word in enumerate(words):
        is_short = short_option and word.startswith(short_option) and not word.startswith("--")
        if (is_short or word.startswith("--method")) and request_method(words, index).upper() not in SAFE_METHODS:
            return True
    return False


def gh_findings(words):
    rest = list(words)
    while rest and rest[0] in ("-R", "--repo"):  # the option before the command group
        rest = rest[2:]
    positional = [word for word in rest if not word.startswith("-")]
    group = positional[0] if positional else ""
    action = positional[1] if len(positional) > 1 else ""
    if group == "release" and action == "create":
        return ["gh release create"]
    if group == "release" and action in GH_RELEASE_WRITES:
        return ["gh release"]
    if group == "gist" and action == "create":
        return ["gh gist"]
    if group == "api":
        body = any(word.startswith(GH_API_BODY_FLAGS) or (word[:2] in ("-f", "-F") and not word.startswith("--"))
                   or "uploads.github.com" in word or "/upload" in word for word in rest)
        return ["gh api upload"] if body or method_writes(rest, "-X") else []
    if any(word.startswith(GH_BODY_FILE_FLAGS) or (word.startswith("-F") and not word.startswith("--"))
           for word in rest):
        return ["gh body file"]
    return []


def curl_uploads(words):
    for word in words:
        if word.startswith(CURL_UPLOAD_LONG_OPTIONS):
            return True
        if word.startswith("-") and not word.startswith("--"):
            for letter in word[1:]:
                if letter in "TFd":
                    return True
                if letter in CURL_SHORT_OPTIONS_WITH_ARGUMENT:
                    break
    return False


def git_pushes(words):
    index = 0
    while index < len(words) and words[index].startswith("-"):
        index += 2 if words[index] in GIT_OPTIONS_WITH_ARGUMENT else 1
    return index < len(words) and words[index] == "push"


def command_findings(command):
    """The labels of the upload commands in the simple command $1 (a list of words)."""
    found = []
    for index, word in enumerate(command):
        program, words = program_name(word), command[index + 1:]
        if program == "gh":
            found.extend(gh_findings(words))
        elif program == "curl" and curl_uploads(words):
            found.append("curl upload")
        elif program == "wget" and (any(w.startswith(WGET_UPLOAD_LONG_OPTIONS) for w in words)
                                    or method_writes(words, "")):
            found.append("wget upload")
        elif program == "git" and git_pushes(words):
            found.append("git push")
        elif (program in OTHER_UPLOAD_PROGRAMS
              or (program == "rsync" and any(":" in w and not w.startswith("-") for w in words))
              or (program == "docker" and "push" in words) or (program == "npm" and "publish" in words)
              or (program == "twine" and "upload" in words) or (program == "aws" and "s3" in words)):
            found.append("other upload")
    return found


def run_findings(text):
    """The names of the upload commands in the `run:` text $1."""
    return [label for command in simple_commands(text) for label in command_findings(command)]


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
    in_release = name == ARCHIVE_WORKFLOW
    publish_only = is_publish_only(step.get("if")) or (job is not None and is_publish_only(job.get("if")))
    if low.startswith("docker://"):
        found.append("%s: uses %s, a container nobody can check" % (where, uses))
    elif low and not low.startswith("./") and ".github/workflows/" in base:
        found.append("%s: uses %s, an external reusable workflow, which may upload" % (where, uses))
    elif low.startswith("./"):
        if local is not None:
            found.extend(local.check(uses, where))
    elif owner_repo in UPLOAD_ACTIONS:
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
    elif owner_repo in CACHE_ACTIONS:
        path = (step.get("with") or {}).get("path")
        if path is None or not str(path).strip():
            found.append("%s: a cache with no path" % where)
        else:
            found.extend("%s: cache path %s is not on the allowlist (ALLOWED_CACHE_PATHS): adding a cache means "
                         "adding its path there, after checking that it holds no linked game, test runner or archive"
                         % (where, line.strip())
                         for line in str(path).splitlines() if line.strip() and line.strip() not in ALLOWED_CACHE_PATHS)
    elif low and owner_repo not in ALLOWED_ACTIONS:
        found.append("%s: uses %s, an action that is not on the allowlist "
                     "(add it to ALLOWED_ACTIONS only after reading it)" % (where, uses))
    for label in run_findings(str(step.get("run", ""))):
        if label == RELEASE_COMMAND and in_release and publish_only:
            continue
        found.append("%s: run: %s (%s)" % (where, label,
                     "allowed only in %s, in a step or job that needs inputs.publish" % ARCHIVE_WORKFLOW
                     if label == RELEASE_COMMAND else "a command that sends files out"))


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
                           ("gh api -f (an implied POST)", "gh api repos/o/r/releases -f tag_name=v1"),
                           ("gh api --raw-field (an implied POST)",
                            "gh api repos/o/r/releases --raw-field tag_name=v1"),
                           ("curl --json", "curl --json @game.json https://example.com/x"),
                           ("gh api --field", "gh api repos/o/r/releases --field tag_name=v1"),
                           ("gh api -F", "gh api repos/o/r/releases -F tag_name=v1"),
                           ("gh api attached -f", "gh api repos/o/r/releases -ftag_name=v1"),
                           ("gh api attached -F", "gh api repos/o/r/releases -Fbody=@notes.txt"),
                           ("gh api --field=", "gh api repos/o/r/releases --field=tag_name=v1"),
                           ("gh api --raw-field=", "gh api repos/o/r/releases --raw-field=tag_name=v1"),
                           ("gh api --method=POST", "gh api repos/o/r/releases --method=POST"),
                           ("gh api --method post", "gh api repos/o/r/releases --method post"),
                           ("gh api -XPOST", "gh api repos/o/r/releases -XPOST"),
                           ("gh api -X put", "gh api repos/o/r/releases/1 -X put"),
                           ("gh api -X PATCH", "gh api repos/o/r/releases/1 -X PATCH"),
                           ("gh api --input", "gh api repos/o/r/releases --input body.json"),
                           ("gh gist create", "gh gist create game.exe"),
                           ("curl -d", "curl -d @game.exe https://example.com/x"),
                           ("curl -d attached", "curl -dfoo https://example.com/x"),
                           ("curl -F attached", "curl -Ffile=@game.exe https://example.com/x"),
                           ("curl -T attached", "curl -Tgame.exe https://example.com/x"),
                           ("curl --upload-file=", "curl --upload-file=game.exe https://example.com/x"),
                           ("curl --data-urlencode", "curl --data-urlencode x@game.exe https://example.com/x"),
                           ("curl --data-binary=", "curl --data-binary=@game.exe https://example.com/x"),
                           ("curl --data-raw", "curl --data-raw x https://example.com/x"),
                           ("curl --form=", "curl --form=file=@game.exe https://example.com/x"),
                           ("curl a short-option cluster with -F", "curl -sSF file=@game.exe https://example.com/x"),
                           ("wget --post-file", "wget --post-file=game.exe https://example.com/x"),
                           ("wget --post-data", "wget --post-data=x https://example.com/x"),
                           ("wget --method", "wget --method=PUT https://example.com/x"),
                           ("wget --body-file", "wget --body-file=game.exe https://example.com/x"),
                           ("git -C x push", "git -C out push origin HEAD"),
                           ("git -c k=v push", "git -c user.name=x push origin HEAD"),
                           ("git --no-pager push", "git --no-pager push origin HEAD"),
                           ("gh api -X 'POST'", "gh api repos/o/r/releases -X 'POST'"),
                           ('gh api -X "POST"', 'gh api repos/o/r/releases -X "POST"'),
                           ('gh api --method "PUT"', 'gh api repos/o/r/releases --method "PUT"'),
                           ("gh api -X $M", "gh api repos/o/r/releases -X $M"),
                           ("gh api --method=$M", "gh api repos/o/r/releases --method=$M"),
                           ("gh api -X DELETEX", "gh api repos/o/r/releases -X DELETEX"),
                           ("gh api an upload path alone", "gh api repos/o/r/releases/1/assets/upload"),
                           ("gh api the uploads host alone", "gh api uploads.github.com/repos/o/r/releases/1/assets"),
                           ("gh api -X=POST", "gh api repos/o/r/releases -X=POST"),
                           ("a program by its path", "/usr/bin/git push origin HEAD"),
                           ("curl by its path", "/usr/bin/curl -T game.exe https://example.com/x"),
                           ("rclone", "rclone copy build r:bucket"),
                           ("npm publish", "npm publish"),
                           ("twine upload", "twine upload dist/x"),
                           ("aws s3", "aws s3 cp game.exe s3://bucket/x"),
                           ("gsutil", "gsutil cp game.exe gs://bucket/x"),
                           ("gh api a write after a read",
                            "gh api repos/o/r/releases | gh api repos/o/r/releases -f a=b"),
                           ("wget --method='PUT'", "wget --method='PUT' https://example.com/x"),
                           ('wget --method "PUT"', 'wget --method "PUT" https://example.com/x'),
                           ("wget --method $M", "wget --method $M https://example.com/x"),
                           ("rsync to host:/dir", "rsync -a build/ host:/dir"),
                           ("rsync to user@host:dir", "rsync -a build/ user@host:dir"),
                           ("scp", "scp game.exe host:dir"),
                           ("git -C 'a b' push", "git -C 'a b' push origin HEAD"),
                           ('git -C "a b" push', 'git -C "a b" push origin HEAD'),
                           ("git --git-dir=x push", "git --git-dir=x push"),
                           ("git push after another command", "make test && git push origin HEAD"),
                           ("gh issue create --body-file", "gh issue create --title t --body-file game.exe"),
                           ("gh issue comment -F", "gh issue comment 1 -F game.exe"),
                           ("gh pr create --body-file=", "gh pr create --body-file=game.exe"),
                           ("gh pr comment -Fattached", "gh pr comment 1 -Fgame.exe"),
                           ("gh pr edit --body-file", "gh pr edit 1 --body-file game.exe"),
                           ("gh -R o/r release upload", "gh -R o/r release upload v1 a.zip"),
                           ("curl continued line", "curl --silent \\\n  -F file=@game.exe https://example.com/x"),
                           ("git push", "git push origin HEAD:refs/heads/x"),
                           ("docker push", "docker push example/game:1")):
        expect("bad: run: " + label, "native.yml", step_workflow("run: |\n  " + command.replace("\n", "\n  ")), "run:")
        expect("bad: run: " + label + " in release.yml without publish", "release.yml",
               step_workflow("run: |\n  " + command.replace("\n", "\n  ")), "run:")
    for label, command in (("curl download", "curl -sSL https://example.com/x | tar -xz"),
                           ("curl -fsSL --output", "curl -fsSL --output sdl2.tar.gz https://example.com/x"),
                           ("curl -o", "curl -sSLo sdl2.tar.gz https://example.com/x"),
                           ("wget -O", "wget -O sdl2.tar.gz https://example.com/x"),
                           ("gh api read", "gh api repos/o/r/actions/runs/1/artifacts"),
                           ("gh api --method GET", "gh api --method GET repos/o/r/actions/runs"),
                           ("gh api --method=get", "gh api --method=get repos/o/r/actions/runs"),
                           ("gh api -X GET", "gh api -X GET repos/o/r/actions/runs"),
                           ("gh api -X HEAD", "gh api -X HEAD repos/o/r/actions/runs"),
                           ("gh api -X DELETE (a deletion sends nothing)",
                            "gh api -X DELETE repos/o/r/actions/artifacts/1"),
                           ("gh api --method=delete", "gh api --method=delete repos/o/r/actions/artifacts/1"),
                           ("curl then unzip -d", "curl -fsSL https://example.com/x -o x.zip && unzip -d out x.zip"),
                           ("curl piped to a script with -d", "curl -fsSL https://example.com/x | bash -s -- -d"),
                           ("curl then tar -F", "curl -fsSL https://example.com/x -o x; tar -F x"),
                           ("gh api read then rm -f", "gh api repos/o/r -q .a && rm -f y"),
                           ("gh api -X 'GET'", "gh api -X 'GET' repos/o/r/actions/runs"),
                           ('gh api --method "delete"', 'gh api --method "delete" repos/o/r/actions/artifacts/1'),
                           ("gh api -X 'DELETE'", "gh api -X 'DELETE' repos/o/r/actions/artifacts/1"),
                           ("wget --method=GET", "wget --method=GET https://example.com/x"),
                           ("wget --method=HEAD", "wget --method=HEAD https://example.com/x"),
                           ("curl with a header", "curl -H 'Accept: d' -fsSL https://example.com/x -o x"),
                           ("an unbalanced quote does not join the commands of its line",
                            'curl -fsSL https://example.com/x -o x && unzip -d out "x'),
                           ("a command substitution then another command's -d",
                            "echo $(curl -fsSL https://example.com/x) ls -d"),
                           ("gh api -X=GET", "gh api -X=GET repos/o/r/actions/runs"),
                           ("a comment line naming a command", "# never git push from here"),
                           ("curl with an attached header argument",
                            "curl -HAccept:d -fsSL https://example.com/x -o x"),
                           ("git log --grep push", "git log --grep push"),
                           ("rsync with a colon in an option", "rsync --exclude=a:b -a x/ y/"),
                           ("rsync locally", "rsync -a build/ out/"),
                           ("gh issue list", "gh issue list --state open"),
                           ("gh run list", "gh run list --limit 3"),
                           ("git remote add and config", "git remote add o u\n  git config push.default simple"),
                           ("git status and log", "git status --short\n  git log -1"),
                           ("git -C x log", "git -C out log -1")):
        expect("good: run: " + label, "native.yml", step_workflow("run: |\n  " + command.replace("\n", "\n  ")), None)
    expect("good: run: gh api graphql read with a GET method", "native.yml",
           step_workflow("run: gh api --method GET repos/o/r/actions/runs"), None)
    release_create = "run: gh release create v1 dist/a.zip"
    expect("bad: gh release create outside release.yml", "native.yml", step_workflow(release_create, job_if="inputs.publish"), "gh release create")
    expect("bad: gh release create in release.yml, no condition", "release.yml", step_workflow(release_create), "gh release create")
    expect("bad: gh release create in release.yml, publish or always", "release.yml",
           step_workflow(release_create, job_if="inputs.publish && true || always()"), "gh release create")
    expect("good: gh release create in release.yml, publish job", "release.yml", step_workflow(release_create, job_if="inputs.publish"), None)
    expect("bad: archive, publish and true or always", "release.yml",
           workflow(["dist/*.zip"], step_if="inputs.publish && true || always()"), ".zip")
    # The allowlist of third-party actions: every one the tree uses passes, any other fails.
    allowed = ("actions/checkout", "actions/download-artifact", "actions/attest-build-provenance",
               "actions/configure-pages", "actions/deploy-pages", "vampire/setup-wsl",
               "mheap/github-action-required-labels", "octokit/request-action")
    if ALLOWED_ACTIONS != allowed:
        failures.append("ALLOWED_ACTIONS is %s, the self-test pins %s: change both, after reading the action"
                        % (ALLOWED_ACTIONS, allowed))
    for action in allowed:
        expect("good: allowed action " + action, "native.yml", step_workflow("uses: %s@v1" % action), None)
    expect("good: a subpath of an allowed action is the allowed action", "native.yml",
           step_workflow("uses: actions/checkout/sub@v4"), None)
    expect("bad: a subpath of an action that is not allowed", "native.yml",
           step_workflow("uses: someone/setup-thing/sub@v4"), "allowlist")
    for action in ("someone/setup-thing@v1", "actions/unknown-thing@v1", "Someone/Setup-Thing@v1",
                   "actions-extra/checkout@v4", "actions/checkout-fork@v4"):
        expect("bad: action not on the allowlist " + action, "native.yml", step_workflow("uses: " + action),
               "allowlist")
    # Caches: an allowlist of exact paths, the ones the tree caches today.
    cache_ok = (
        "tools/bin2c/bin2c", "tools/gbafix/gbafix", "tools/gbagfx/gbagfx", "tools/jsonproc/jsonproc",
        "tools/mapjson/mapjson", "tools/mid2agb/mid2agb", "tools/preproc/preproc", "tools/ramscrgen/ramscrgen",
        "tools/rsfont/rsfont", "tools/scaninc/scaninc", "tools/trainerproc/trainerproc",
        "tools/compresSmol/compresSmol", "tools/compresSmol/compresSmolTilemap", "tools/wav2agb/wav2agb",
        "tools/patchelf/patchelf", "tools/mgba-rom-test-hydra/mgba-rom-test-hydra",
        "~/.cache/ccache", "wsl-cache/ccache", "build/assets-native", "build/sdl2-mingw", "wsl-cache/sdl2-mingw",
    )
    if ALLOWED_CACHE_PATHS != cache_ok:
        failures.append("ALLOWED_CACHE_PATHS is %s, the self-test pins %s: change both, after checking the path"
                        % (ALLOWED_CACHE_PATHS, cache_ok))
    cache_message = "ALLOWED_CACHE_PATHS"

    def cache_workflow(paths, action="actions/cache@v4"):
        lines = "".join("    %s\n" % line for line in paths)
        return step_workflow("uses: %s\nwith:\n  key: k\n  path: |\n" % action + lines)

    for path in cache_ok:
        expect("good: cache of " + path, "native.yml", cache_workflow([path]), None)
    expect("good: today's tool block and ccache in one path", "native.yml", cache_workflow(cache_ok), None)
    expect("good: blank lines and indentation in a path block", "native.yml",
           cache_workflow(["~/.cache/ccache", "", "  build/sdl2-mingw  "]), None)
    expect("bad: the path in the message is the line, stripped", "native.yml",
           cache_workflow(["~/.cache/ccache", "",
                           "  build/native64  "]), "cache path build/native64 is not on the allowlist")
    expect("good: a one-line path", "native.yml",
           step_workflow("uses: actions/cache@v4\nwith:\n  key: k\n  path: ~/.cache/ccache\n"), None)
    for path in ("out/pokeemerald-native", "pokeemerald-native-*", "$GITHUB_WORKSPACE/build/native64",
                 "~/work/r/r/build/native64", "build/native64", "x.EXE", "x.tgz", "build/native64/pokeemerald-native",
                 "build/native64-windows", "platform/build", "dist", "dist/pokeemerald-native.tar.gz", "build",
                 "build/**",
                 "*", ".", "./build/assets-native", "build/assets-native/", "Build/assets-native",
                 "BUILD/ASSETS-NATIVE",
                 "~/.cache/ccache/", "tools/gbagfx/GBAGFX", "tools/gbagfx", "out/game.exe/", "build\\native64",
                 "${{ matrix.path }}", "/home/runner/work/r/r/build/native64", "../r/build/native64",
                 "!~/.cache/ccache/tmp"):
        expect("bad: cache of " + path, "native.yml", cache_workflow(["~/.cache/ccache", path]), cache_message)
    for action in ("actions/cache/save@v4", "actions/cache/restore@v4", "Actions/Cache@v4"):
        expect("bad: " + action + " is checked by the cache rule", "native.yml",
               cache_workflow(["build/native64"], action), cache_message)
        expect("good: " + action + " with an allowed path", "native.yml", cache_workflow(["~/.cache/ccache"], action),
               None)
    expect("bad: a cache with no path", "native.yml", step_workflow("uses: actions/cache@v4\nwith:\n  key: k\n"),
           "a cache with no path")

    def expect_tree(label, files, wanted):
        """files: path -> text, written under a scratch root; wanted as in expect()."""
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

    caller = "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - uses: %s\n"
    action_of = "name: a\nruns:\n  using: composite\n  steps:\n    - %s\n"
    upload_step = "uses: actions/upload-artifact@v4\n      with:\n        path: build/game.exe"
    expect_tree("bad: a composite action outside .github/actions", {
        WORKFLOW_DIR + "/w.yml": caller % "./tools/x", "tools/x/action.yml": action_of % upload_step}, ".exe")
    expect_tree("bad: a composite action nested under .github/actions", {
        WORKFLOW_DIR + "/w.yml": caller % "./.github/actions/a/b",
        ACTION_DIR + "/a/b/action.yml": action_of % upload_step}, ".exe")
    expect_tree("bad: a composite action named action.yaml", {
        WORKFLOW_DIR + "/w.yml": caller % "./tools/x", "tools/x/action.yaml": action_of % upload_step}, ".exe")
    expect_tree("bad: a local action that does not exist", {WORKFLOW_DIR + "/w.yml": caller % "./tools/missing"},
                "missing")
    expect_tree("bad: a local action that reaches outside the root",
                {WORKFLOW_DIR + "/w.yml": caller % "./../elsewhere"}, "outside")
    expect_tree("bad: an action that uses a bad local action", {
        WORKFLOW_DIR + "/w.yml": caller % "./tools/x", "tools/x/action.yml": action_of % "uses: ./tools/y",
        "tools/y/action.yml": action_of % upload_step}, ".exe")
    expect_tree("good: a local action that uses itself ends", {
        WORKFLOW_DIR + "/w.yml": caller % "./tools/x", "tools/x/action.yml": action_of % "uses: ./tools/x"}, None)
    expect_tree("good: a clean local action outside .github/actions", {
        WORKFLOW_DIR + "/w.yml": caller % "./tools/x",
        "tools/x/action.yml": action_of % "run: echo\n      shell: bash"}, None)
    expect_tree("good: a local reusable workflow that exists", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    uses: ./.github/workflows/x.yml\n",
        WORKFLOW_DIR + "/x.yml": "on: workflow_call\njobs:\n  k:\n    runs-on: x\n    steps:\n      - run: echo\n"},
        None)
    expect_tree("bad: a job-level uses of a local action that is missing", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    uses: ./missing\n"}, "missing")
    expect_tree("bad: a local file that is not a workflow", {
        WORKFLOW_DIR + "/w.yml": caller % "./file.txt", "file.txt": "x\n"},
        "neither a workflow nor an action directory")
    expect_tree("bad: a workflow-like file outside .github/workflows", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    uses: ./other/x.yml\n",
        "other/x.yml": "on: workflow_call\n"},
        "neither a workflow nor an action directory")
    expect_tree("bad: an action.yaml under .github/actions that nothing uses", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - run: echo\n",
        ACTION_DIR + "/idle/action.yaml": action_of % upload_step}, ".exe")
    expect_tree("bad: an action.yml under .github/actions that nothing uses", {
        WORKFLOW_DIR + "/w.yml": "on: push\njobs:\n  j:\n    runs-on: x\n    steps:\n      - run: echo\n",
        ACTION_DIR + "/idle/action.yml": action_of % upload_step}, ".exe")
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
