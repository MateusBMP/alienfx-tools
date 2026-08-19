---
name: update-linux-roadmap
description: Refresh Doc/linux_roadmap/ with new upstream discussion/issue activity, kernel/prior-art project progress, and local implementation changes since the last research pass. Use when the user asks to update, refresh, re-run, or sync the Linux roadmap/port analysis, or asks "what's new" regarding Linux support for this project.
---

# Update Linux Roadmap

Incrementally refreshes `Doc/linux_roadmap/` — never regenerate it from scratch. The
whole point of `00-sources.md` is that a later pass only re-checks what might have
changed and edits the specific docs affected. Treat a full re-derivation of the
roadmap as a failure mode, not thoroughness.

## Before doing anything

Read `Doc/linux_roadmap/00-sources.md` in full — its "Pinned to" line (commit +
date), all five source tables (A–E), and its "How to extend this research
incrementally" checklist. That checklist is the actual procedure; the steps below
expand on it with concrete commands. Do not run a fresh multi-agent codebase
exploration or repeat searches already logged there — this skill exists specifically
to avoid that cost.

## Steps

1. **Upstream repo activity** (`T-Troll/alienfx-tools`). Re-run the issue search and
   diff against `00-sources.md` section A:
   ```
   gh search issues --repo T-Troll/alienfx-tools linux --json number,title,state,updatedAt
   ```
   (or the `curl https://api.github.com/search/issues?q=repo:T-Troll/alienfx-tools+linux`
   equivalent if `gh` isn't available). Only read issues not already in section A's
   table, or whose comment activity is newer than the pinned research date.
   Specifically re-check [discussion #421](https://github.com/T-Troll/alienfx-tools/discussions/421)
   and [issue #434](https://github.com/T-Troll/alienfx-tools/issues/434) for new
   comments — both were open/active threads at last research and are the most likely
   source of new porting progress from other contributors.

2. **Kernel driver ABI**. Re-fetch
   [docs.kernel.org/admin-guide/laptops/alienware-wmi.html](https://docs.kernel.org/admin-guide/laptops/alienware-wmi.html)
   and [docs.kernel.org/wmi/devices/alienware-wmi.html](https://docs.kernel.org/wmi/devices/alienware-wmi.html)
   and diff against what `05-alienfan-sdk-thermal.md` and `00-sources.md` section B
   currently claim — new sysfs attributes, an expanded model allowlist, new module
   parameters, or (if it ever happens) `_HID`-based auto-detection replacing the
   allowlist would all be material changes.

3. **Prior-art project progress**. Check `tr1xem/alienfx-linux` and `tr1xem/AWCC` for
   commits/releases since the pinned date (`gh api repos/tr1xem/alienfx-linux/commits
   --jq '.[0].commit.author.date'`, same for `AWCC`). Both were early-stage projects at
   last research — if either has materially progressed (new API version support,
   daemon/D-Bus interface shipped, packaging added), re-fetch the relevant files and
   update `04`/`05` (for `alienfx-linux`) or `09`/`10` (for `AWCC`) accordingly. Also
   re-run the original `WebSearch` queries logged in section E to catch any *new*
   prior-art project that didn't exist before. While here, consider finally doing a
   real read of `trackmastersteve/alienfx` and `rsm-gh/akbl` — section C flags both as
   only search-snippet-reviewed, a known gap from the original pass.

4. **Local implementation progress**. `git log --oneline <pinned-commit>..HEAD` in
   this repo. The roadmap assumes zero Linux-facing code exists yet — if that's no
   longer true (a `CMakeLists.txt` appeared, a compat-layer header landed, any file
   under a new `linux/` tree, etc.), this is the most important kind of update: it
   means specific milestones in `17-milestones.md` have moved from planned to
   in-progress/done and the affected doc(s) should say so, not just describe the
   still-unbuilt plan. Cross-check new/changed source files against `00-sources.md`
   section D to see if a file central to the protocol docs (`AlienFX_SDK.cpp`,
   `alienfan-controls.h`, etc.) changed upstream in a way that invalidates a specific
   `path:line` citation in `04`/`05`/`06`.

5. **Apply targeted updates only.** For each new fact found, edit the specific
   roadmap doc(s) listed in `00-sources.md`'s "Feeds" column for that source — don't
   touch unrelated docs. Preserve existing style: `path:line` references into this
   repo, `#NNN` links for upstream issues, prose that states what changed and why it
   matters (not just "updated").

6. **Update the ledger itself.**
   - Add new rows to whichever of `00-sources.md`'s tables (A–E) got new sources,
     in the same format as existing rows.
   - Update the "Pinned to" line at the top to the new commit hash and today's date.
   - If a checklist item in "How to extend this research incrementally" turned up
     nothing new, that's fine — don't force an edit; just leave the pin date current
     so the *next* pass knows this angle was checked.

7. **Verify before finishing.** Re-check that every `path:line` reference in touched
   docs still resolves (files may have shifted line numbers) and that internal
   markdown links still resolve. A quick script (adjust the file list to what you
   touched):
   ```bash
   python3 - <<'EOF'
   import re, os, glob
   from collections import defaultdict
   basemap = defaultdict(list)
   for root, _, fnames in os.walk('.'):
       if '.git' in root.split(os.sep): continue
       for fn in fnames: basemap[fn].append(os.path.join(root, fn))
   files = ["CLAUDE.md"] + sorted(glob.glob("Doc/linux_roadmap/*.md"))
   pat = re.compile(r'`([A-Za-z0-9_./\-]+\.(?:cpp|h|hpp|c|cs|rc|csv|ini|sln|vcxproj|vcxitems|txt|json)):(\d+)')
   missing = []
   for f in files:
       for m in pat.finditer(open(f, encoding='utf-8').read()):
           path, line1 = m.group(1), int(m.group(2))
           cands = [path, os.path.join(os.path.dirname(f), path)]
           resolved = next((c for c in cands if os.path.isfile(c)), None)
           if not resolved:
               opts = basemap.get(os.path.basename(path), [])
               resolved = opts[0] if len(opts) == 1 else None
               if len(opts) > 1: missing.append((f, path, "AMBIGUOUS", opts)); continue
           if not resolved: missing.append((f, path, "NOT FOUND")); continue
           n = sum(1 for _ in open(resolved, encoding='utf-8', errors='ignore'))
           if line1 > n: missing.append((f, f"{path}:{line1}", f"only {n} lines"))
   print(f"{len(missing)} problems" if missing else "all refs resolve")
   for m in missing: print(" ", m)
   EOF
   ```

## Output

Report back as a delta, not a restatement of the whole roadmap: what was checked,
what (if anything) was new, which docs were edited and why, and what the updated pin
date/commit is. If nothing changed anywhere, say that plainly rather than inventing
an update.

## Constraints

- This is documentation maintenance. Don't write or modify Linux port source code as
  part of this skill unless the user separately asks for implementation work — if
  local implementation progress is found (step 4), reflect it in the docs, don't
  extend it.
- Don't regenerate a doc wholesale to incorporate a small fact — edit it in place.
- Don't silently overturn a locked-in roadmap decision (cross-platform in-tree build,
  full scope, kernel-sysfs-primary fan backend, Qt 6 GUI — see `README.md`'s
  "Decisions this roadmap assumes"). If new evidence genuinely challenges one of
  these, surface it to the user and ask before rewriting the decision.
- If a source is unreachable or a search turns up nothing new, say so — don't leave
  the ledger's pin date stale by skipping the check silently, and don't fabricate
  findings to appear thorough.
