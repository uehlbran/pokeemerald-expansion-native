#!/usr/bin/env python3
"""check_workflow_uploads.py: no workflow uploads the game (docs/DECISIONS.md, "No workflow uploads the game").

Reads every workflow under .github/workflows and fails on any upload step whose `path` could
match a game binary or archive. The rule is that a workflow artifact of a public repository is
downloadable by anyone logged in to GitHub, so only text may be uploaded. For every
`actions/upload-artifact` step (and any other upload action listed in UPLOAD_ACTIONS), every
line of `with.path` must be a pattern that:

  * is not an exclusion (`!pattern`: a denylist);
  * has no brace (`{a,b}`: @actions/glob sets nobrace, so it matches nothing, and a pattern that
    reads as an allowlist must be one);
  * has no `${{ }}` expression (it cannot be checked);
  * names its files by an extension in its last path component: not a directory, not `*`, not
    `**`, not a name without an extension;
  * has an extension without a wildcard in it, and in TEXT_EXTENSIONS.

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

# Text the workflows really upload: logs, the smoke scripts' dumps and lists, the replays' scripts
# and diffs. Not .ppm (screenshots of the game), .sav and .rtc (saves and clocks, which stay in
# the job that makes them).
TEXT_EXTENSIONS = (".log", ".txt", ".diff", ".replay", ".all", ".known-patterns")

# Named for the message only: anything not in TEXT_EXTENSIONS fails.
BINARY_EXTENSIONS = (".exe", ".dll", ".so", ".dylib", ".gba", ".elf", ".zip", ".tar.gz", ".7z")

# Archives that release.yml may upload, with `publish`.
ARCHIVE_EXTENSIONS = (".tar.gz", ".zip")
ARCHIVE_WORKFLOW = "release.yml"

UPLOAD_ACTIONS = ("actions/upload-artifact", "actions/upload-pages-artifact")

# The forms of an `if` that mean "only with publish", split on && by conjunct.
PUBLISH_CONJUNCTS = ("inputs.publish", "inputs.publish == true", "inputs.publish == 'true'",
                     "github.event.inputs.publish == 'true'", "github.event.inputs.publish == true")


# The one upload of a directory: upstream's docs.yml publishes its mdBook output (the
# documentation site, no game), filtered to upstream's branches, so it does not run here
# (UPSTREAM_MODIFICATIONS.md). Exact workflow, action and path; nothing else.
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
    if ext in TEXT_EXTENSIONS:
        return []
    if ext in ARCHIVE_EXTENSIONS and archive_allowed:
        return []
    if ext in BINARY_EXTENSIONS:
        why = "a binary or archive extension"
        if ext in ARCHIVE_EXTENSIONS:
            why += " (allowed only in %s, in a job or step that needs inputs.publish)" % ARCHIVE_WORKFLOW
        return ["%s %s" % (ext, why)]
    return ["the extension %s is not on the text list (%s)" % (ext, " ".join(TEXT_EXTENSIONS))]


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
        for number, step in enumerate((job or {}).get("steps") or [], 1):
            uses = str(step.get("uses", ""))
            if not any(uses.startswith(action + "@") or uses == action for action in UPLOAD_ACTIONS):
                continue
            where = "%s: job %s, step %d (%s)" % (name, job_id, number, step.get("name", uses))
            with_ = step.get("with") or {}
            path = with_.get("path")
            if path is None or not str(path).strip():
                found.append("%s: an upload with no path" % where)
                continue
            archive_allowed = (name == ARCHIVE_WORKFLOW
                               and (is_publish_only(step.get("if")) or is_publish_only(job.get("if"))))
            action = uses.split("@")[0]
            for line in str(path).splitlines():
                pattern = line.strip()
                if (name, action, pattern) in DIRECTORY_EXCEPTIONS:
                    continue
                if pattern:
                    found.extend("%s: path %s %s" % (where, pattern, problem)
                                 for problem in problems_of_pattern(pattern, archive_allowed))
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
            "out/**/*.known-patterns", "platform/build/64-windows/test/*.log", "suite-linux64.log"]
    expect("good text patterns", "native.yml", workflow(good), None)
    expect("good, step if failure()", "native.yml", workflow(good, step_if="failure()"), None)
    for ext in (".exe", ".dll", ".so", ".dylib", ".gba", ".elf", ".7z"):
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
    expect("bad: .sav", "native.yml", workflow(["out/**/*.sav"]), ".sav")
    expect("bad: .rtc", "native.yml", workflow(["out/**/*.rtc"]), ".rtc")
    expect("bad: .ppm", "native.yml", workflow(["out/**/*.ppm"]), ".ppm")
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
