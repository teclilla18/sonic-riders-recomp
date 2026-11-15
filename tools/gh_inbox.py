"""Newest activity on the GitHub repo: issues, issue comments and discussions (start and end of every session).

    python tools/gh_inbox.py [--n 8]

Needs the GitHub CLI (`gh`, logged in). Prints one line per item, newest first; read the full thread with
`gh issue view N --comments` or the discussion's URL.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

REPO = "teclilla18/sonic-riders-recomp"


def gh():
    for p in (shutil.which("gh"), r"C:\Program Files\GitHub CLI\gh.exe"):
        if p and os.path.exists(p):
            return p
    sys.exit("gh_inbox: the GitHub CLI (gh) was not found")


def gh_env():
    """devkitPro's MSYS python (the first `python` on PATH in Git Bash) starts with almost no Windows
    environment; without APPDATA gh cannot find its config and says "not logged in". Put the folders back."""
    env = dict(os.environ)
    if "APPDATA" not in env and sys.platform in ("msys", "cygwin"):
        user = env.get("USERNAME") or os.path.basename(env.get("HOME", "").rstrip("/"))
        if user:
            home = env.get("USERPROFILE") or "C:\\Users\\" + user
            env.setdefault("USERPROFILE", home)
            env["APPDATA"] = home + "\\AppData\\Roaming"
            env.setdefault("LOCALAPPDATA", home + "\\AppData\\Local")
    return env


def run(*args):
    r = subprocess.run([gh(), *args], capture_output=True, text=True, encoding="utf-8", env=gh_env())
    if r.returncode:
        sys.exit("gh_inbox: gh %s failed: %s" % (args[0], r.stderr.strip()[:300]))
    return json.loads(r.stdout)


def short(s, n=90):
    s = " ".join((s or "").split())
    return s if len(s) <= n else s[: n - 1] + "..."


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # emoji in comments on a cp1252 console
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8, help="items per list (default 8)")
    n = ap.parse_args().n
    print("== Issues (most recently updated)")
    for i in run("issue", "list", "--repo", REPO, "--state", "all", "--limit", str(n), "--search", "sort:updated-desc",
                 "--json", "number,title,state,updatedAt,author"):
        print("#%d %s %s %s: %s" % (i["number"], i["updatedAt"][:16], i["state"], i["author"]["login"], short(i["title"])))
    print("== Issue comments (newest)")
    for c in run("api", "repos/%s/issues/comments?sort=updated&direction=desc&per_page=%d" % (REPO, n)):
        print("#%s %s %s: %s" % (c["issue_url"].rsplit("/", 1)[1], c["updated_at"][:16], c["user"]["login"], short(c["body"])))
    owner, name = REPO.split("/")
    q = ('{repository(owner:"%s",name:"%s"){discussions(first:%d,orderBy:{field:UPDATED_AT,direction:DESC}){nodes{'
         'number title updatedAt author{login} comments(last:1){nodes{author{login} updatedAt body}}}}}}' % (owner, name, n))
    print("== Discussions (most recently updated)")
    for d in run("api", "graphql", "-f", "query=" + q)["data"]["repository"]["discussions"]["nodes"]:
        last = d["comments"]["nodes"]
        tail = " | last: %s %s: %s" % (last[0]["author"]["login"] if last[0]["author"] else "?", last[0]["updatedAt"][:16],
                                       short(last[0]["body"], 60)) if last else ""
        print("#%d %s %s: %s%s" % (d["number"], d["updatedAt"][:16], (d["author"] or {}).get("login", "?"), short(d["title"], 60), tail))


if __name__ == "__main__":
    main()
