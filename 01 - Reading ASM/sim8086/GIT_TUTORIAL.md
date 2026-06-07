# Daily Git Workflow — A Beginner's Guide

A short, practical reference for the everyday git commands you'll actually use.
Everything here is done from the terminal (command line).

---

## 0. Core idea (read this once)

Git tracks your project in **three places**:

| Place | What it is |
|-------|-----------|
| **Working directory** | The files you edit on disk right now |
| **Staging area (index)** | Changes you've marked to go into the next commit |
| **Repository (.git)** | The permanent history of committed snapshots |

The normal flow is: **edit → stage → commit → push**.

A **branch** is just a movable label pointing at a commit. You do your work on a
branch, then merge it back into `main`.

---

## 1. One-time setup

```bash
git config --global user.name  "Your Name"
git config --global user.email "you@example.com"

# Make 'main' the default branch name for new repos
git config --global init.defaultBranch main
```

Starting a repo:

```bash
git init                 # turn the current folder into a git repo
# or
git clone <url>          # copy an existing remote repo to your machine
```

---

## 2. The daily loop

### Check what's going on (do this constantly)

```bash
git status               # what's changed / staged / untracked
git diff                 # see unstaged changes line-by-line
git diff --staged        # see what's staged for the next commit
git log --oneline -10    # last 10 commits, compact
```

### Stage and commit

```bash
git add file.txt         # stage one file
git add .                # stage everything changed in this folder down
git restore --staged f   # unstage a file (keeps your edits)

git commit -m "Add bracketed memory operand decoding"
```

Commit small and often. Each commit should be one logical change.

### Sync with the remote

```bash
git pull                 # fetch + merge remote changes into your branch
git push                 # send your commits to the remote
```

---

## 3. Feature-branch workflow

Never work directly on `main`. Make a branch per feature or fix.

```bash
# Start fresh from an up-to-date main
git switch main
git pull

# Create and switch to a new branch
git switch -c feature/decode-mov

# ...edit, add, commit as many times as you like...

# First push: set the upstream so future pushes are just 'git push'
git push -u origin feature/decode-mov
```

Switching between branches:

```bash
git switch main              # go back to main
git switch feature/decode-mov
git branch                   # list local branches (* = current)
```

---

## 4. Merging a branch into main yourself (solo workflow)

When you're working **alone**, you don't need a Pull Request — a PR is really just a
place for *other people* to review your code before it lands. Solo, you can merge
the branch into `main` directly on your machine.

```bash
# Make sure your feature branch is committed and clean
git status

# Switch to main and get it up to date
git switch main
git pull

# Merge your feature branch in
git merge feature/decode-mov

# Push the updated main to the remote (if you have one)
git push

# Clean up the finished branch
git branch -d feature/decode-mov            # delete locally
git push origin --delete feature/decode-mov # delete on remote (if pushed)
```

Notes:
- If `git merge` reports a conflict, jump to **Section 9** to resolve it, then the
  merge completes (see **Section 9**).
- `git branch -d` (lowercase d) only deletes a branch that's been fully merged —
  it's a safety check. Use `-D` to force-delete an unmerged branch.
- Prefer a clean single commit? Use `git merge --squash feature/decode-mov`, then
  `git commit` — this folds all the branch's commits into one before it lands.

> You can even skip branches entirely for tiny solo changes and just commit
> straight to `main`. Branches still pay off when you want to experiment without
> disturbing a known-good `main`.

---

## 5. Update main without switching branches (preferred Unity flow)

This is the cleanest flow for this project. It keeps Unity on the feature branch —
no asset reimport from a branch switch — while updating both GitHub's `main` and your
local `main`. It uses **rebase**, which replays your commits on top of the latest
`main` for a tidy, linear history (no merge commits).

> Safe because the feature branch is **yours and unshared**: you rebase it, push the
> result to `main`, then delete it — you never ask anyone to pull the rewritten
> branch. Don't rebase a branch others are already working on.

**1. Make sure the feature branch is clean.**

```bash
git status
```

If there are uncommitted changes, commit or stash them before rebasing.

**2. Download the latest branch state from GitHub.**

```bash
git fetch origin
```

This updates `origin/main` locally, but it does not change your checked-out files.

**3. Rebase the feature branch onto GitHub's latest `main`.**

```bash
git rebase origin/main
```

This replays your feature-branch commits on top of the latest remote `main`.

**4. If Git reports conflicts, inspect and resolve them** (see Section 9).

```bash
git status
git diff --name-only --diff-filter=U
```

Open each conflicted file, resolve the markers, then mark them resolved and continue:

```bash
git add path/to/resolved-file
git rebase --continue
```

(Rebase replays one commit at a time, so the same conflict can resurface per commit.)

**5. Validate in Unity while still on the feature branch.**

- Let imports finish.
- Check the Console.
- Run tests or a quick play-mode smoke test if relevant.

**6. Push the current commit to GitHub's `main`.**

```bash
git push origin HEAD:main
```

`HEAD` is the commit you're on; `HEAD:main` pushes that commit to the remote branch
named `main`. Because you just rebased, this is a clean fast-forward.

**7. Update local `main` without switching branches.**

```bash
git fetch origin
git branch -f main origin/main
```

This makes local `main` match GitHub's `main` without making Unity reload the
project from a branch switch.

**8. Optional: switch to `main` once both branches match.**

```bash
git status
git switch main
```

### If the push is rejected (`non-fast-forward`)

This means GitHub's `main` has commits your local `main` does not. Inspect first:

```bash
git fetch origin
git log --oneline --graph --decorate --left-right HEAD...origin/main
```

Lines starting with `<` are only local; lines starting with `>` are only on GitHub.
If the remote changes are expected, rebase on top of them and retry:

```bash
git rebase origin/main
git push origin HEAD:main
git fetch origin
git branch -f main origin/main
```

Avoid force-pushing to `main` unless you deliberately want to rewrite GitHub history.

### Clean up the merged branch

After `main` is pushed and validated, delete the feature branch:

```bash
git push origin --delete feature/my-feature-name   # remote
git branch -d feature/my-feature-name              # local
```

If Git says the local branch isn't fully merged, compare before forcing:

```bash
git log --oneline --left-right main...feature/my-feature-name
git diff --stat main..feature/my-feature-name
```

If it only holds old duplicate history or files intentionally removed from `main`,
force-delete the pointer:

```bash
git branch -D feature/my-feature-name
```

---

## 6. Opening a Pull Request from the terminal (GitHub CLI)

> Optional — use this when collaborating with a team. Solo, **Section 4 or 5** is simpler.


The [GitHub CLI](https://cli.github.com/) `gh` lets you open and merge PRs without
leaving the terminal. Authenticate once with `gh auth login`.

```bash
# After pushing your branch:
gh pr create --fill          # uses your commits to fill title/body
# or write them yourself:
gh pr create --title "Decode MOV instruction" --body "Implements register/memory MOV."

gh pr view --web             # open the PR in your browser
gh pr status                 # see your PRs at a glance
gh pr checks                 # CI status for the current branch's PR

# When approved and CI is green:
gh pr merge --squash --delete-branch
```

`--squash` combines all branch commits into one tidy commit on `main`.
`--delete-branch` cleans up the branch locally and remotely.

After merging, get back to a clean state:

```bash
git switch main
git pull
```

---

## 7. Writing good commit messages

A clear message is a gift to future-you. Common style:

```
<short summary, imperative mood, ~50 chars>

<optional body: WHY the change, wrapped at ~72 chars.
Explain context that the diff itself can't show.>
```

**Imperative mood** = write the summary as a command:
- ✅ `Fix off-by-one in displacement parsing`
- ❌ `Fixed off-by-one` / `Fixes the bug` / `displacement stuff`

Optional **Conventional Commits** prefixes make history scannable:

| Prefix | Use for |
|--------|---------|
| `feat:` | a new feature |
| `fix:` | a bug fix |
| `docs:` | documentation only |
| `refactor:` | code change that isn't a feature or fix |
| `test:` | adding/fixing tests |
| `chore:` | tooling, build, housekeeping |

Example: `feat: decode 16-bit immediate-to-register MOV`

---

## 8. Undoing and fixing mistakes

> Rule of thumb: if you **haven't pushed** yet, you can rewrite freely.
> Once pushed and shared, prefer `revert` over rewriting history.

### Fix the last commit (not pushed)

```bash
git commit --amend -m "Better message"   # change message / add staged files
```

### Discard changes

```bash
git restore file.txt          # throw away unstaged edits to a file (CAREFUL)
git restore .                 # discard all unstaged edits
git restore --staged file.txt # unstage but keep the edits
```

### Move the branch pointer back

```bash
git reset --soft HEAD~1   # undo last commit, KEEP changes staged
git reset --mixed HEAD~1  # undo last commit, keep changes unstaged (default)
git reset --hard HEAD~1   # undo last commit AND delete the changes (DANGER)
```

### Safely undo a commit that's already pushed

```bash
git revert <commit-hash>  # makes a NEW commit that reverses an old one
```

### Stash: shelve work-in-progress temporarily

```bash
git stash            # save dirty changes and clean the working dir
git switch main      # ...go do something else...
git switch -         # back to your branch
git stash pop        # bring your changes back
git stash list       # see all stashes
```

---

## 9. Handling merge conflicts

Conflicts happen when two changes touch the same lines. Git pauses and asks you to
decide. Don't panic — it's a normal part of the workflow.

```bash
git pull           # ...CONFLICT! Git stops and marks the files
git status         # shows files "Unmerged" — these need your attention
```

Open a conflicted file. Git inserts markers:

```
<<<<<<< HEAD
your version of the line
=======
the incoming version of the line
>>>>>>> origin/main
```

To resolve:
1. Edit the file: delete the `<<<<<<<`, `=======`, `>>>>>>>` markers and keep the
   final correct content (which may be one side, the other, or a blend of both).
2. Stage the resolved file and finish:

```bash
git add file.txt
git commit          # completes the merge (or 'git merge --continue')
```

Bail out and start over:

```bash
git merge --abort   # cancel the merge, return to pre-pull state
```

---

## 10. Quick cheat sheet

```bash
git status                       # where am I, what's changed
git switch -c feature/x          # new branch
git add .                        # stage everything
git commit -m "feat: ..."        # commit
git push -u origin feature/x     # push (first time, optional)

# Solo (preferred): update main without leaving the branch — Section 5
git fetch origin
git rebase origin/main           # replay your work on latest main
git push origin HEAD:main        # fast-forward remote main to your commit
git branch -f main origin/main   # match local main, no branch switch
git branch -d feature/x          # tidy up

# Team: open a PR instead of merging directly
gh pr create --fill
gh pr merge --squash --delete-branch
```

**When in doubt, run `git status`** — it almost always tells you what to do next.
