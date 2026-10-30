# Guide for agents

This repository builds a speedrun practice ROM for Glover (USA, NGVE) for the N64. It is built from the matching
decompilation [gloveworld](https://github.com/Eddie-Lack/gloveworld), which is a submodule here. The result is
handed out as a patch that players apply to their own ROM. Read this file first, then [docs/plan.md](docs/plan.md).

## Rules

- **Never commit a ROM**, or any file that holds data copied from one: a patched ROM, memory dumps, savestates,
  extracted textures or sounds. Patch files (BPS, IPS) are built, not committed, unless a release asks for one.
  Demo reels and other videos are not committed either.
- **Never use leaked original source code**, the N64 SDK's source, or code from other practice ROMs or ports, in
  any form. Public documentation and your own design are fine. gloveworld's code, tools and docs may be used.
- **The Expansion Pak is required.** Practice code, its data and savestates live in the upper 4 MB of RAM. Without
  the Expansion Pak the ROM shows a clear message and stops.
- **With every practice feature off, the game must play as the retail game does**: the same logic on every game
  frame. During gameplay there is no input lag and no change to the picture. Small differences that do not change
  gameplay are fine, such as a load that ends a video frame earlier or later, or a value that only records time.
  Test this (docs/plan.md, "Faithfulness"). Two
  exceptions: the in-game timer bug after a Time Trial is always fixed (docs/plan.md, "Timers"), and the ball
  controls are Easy by default (docs/plan.md, "Ball controls").
- **Treat gloveworld as a separate project, made by someone else.** Use it as it is. Everything the practice build
  needs lives in this repository. When a change to gloveworld would help the practice ROM, or you find an error in
  gloveworld, note it in [docs/gloveworld.md](docs/gloveworld.md) and tell the owner if entries need a decision.
- **Keep every game address as in retail.** A hook replaces a call with `PRACTICE_WRAP(fn)()`; it never adds code
  to a game function. `tools/checklayout.py` fails the build if a game symbol moves (docs/build.md, "Hooks").
- **Write all prose (docs, comments, commits, PR text) in plain language** (ISO 24495-1): short sentences, one
  idea each, terms defined on first use. No em dashes. No AI model names. Times in UTC-7 Yukon time.
- **Do not end sentences with a purpose clause** that explains why the preceding detail matters, such as "so
  that X instead of Y". State the fact and stop.
- **Files for human readers say only what those readers need.** The README is for players and contributors: what
  the ROM does, how to apply the patch, a link to the wiki and how to build. The controls and each feature are
  described in `wiki/`, one page per menu. Do not state the obvious, compare the project
  with other projects, or repeat rules from this file there. Agent-facing rules and process go in this file and in
  `docs/`.
- **The README has a Thanks section.** The owner keeps it short, with a link for each project. Ask the owner
  before adding an entry.
- **Write docs for players in the owner's style** (see "Writing style for docs" below).
- Comment code where the reason is not obvious. Follow the style of the code around it.

## Be creative

Add tools generously. Nothing is too extreme for a practice ROM. A tool that bends or breaks the game is welcome,
especiallly when it helps practice, glitch hunting or understanding the game. Build what is clearly useful and 
fits the plan.

Ideas that are absurd or questionable need the owner's decision. Do not drop them without asking. Making a demo
of them for demonstration is ok. Add each one to [docs/decisions.md](docs/decisions.md) with a short description,
the cost, the risks and your recommendation. Keep that file sorted by its sections and update an entry when you
learn more.

## Writing style for docs

The owner rewrote the README and the wiki in this style. Write every page for players (the README, `wiki/`, the
patch page) like it. Copy the pages the owner edited when unsure.

- Talk to the player as "you", in a friendly, casual voice. Contractions are fine ("don't", "can't").
- Say what the player can do, and cut the rest: no "off at start, not saved", no reasons why a combination does
  not happen in play, no glossary, nothing obvious.
- One fact per line. Break a long paragraph into short lines with blank lines between them, or into a bullet list.
- No full stop at the end of a table cell, a bullet or a one-line paragraph.
- Table headers say "Option", not "Item". A sub menu's row says `_Sub menu_`.
- Links to other pages are in italics: `_Back to the [Home page](Home.md)_` at the top, and
  `_see [Warp](Warp.md)_` or `_(see [Game](Game.md))_` in the text.
- A list of choices or names goes in backticks: `` `off, rare, sometimes, often` ``.
- Put an important warning in a heading or with "!" ("## The Expansion Pak is required!!").
- A side fact starts with "Note:". Unfinished features are marked "WIP".
- Common player words are fine: rng, free cam, hotkeys, KBs.
- Keep the README short: one line on what the ROM is, the Expansion Pak, how to apply the patch, links to the
  wiki, Thanks, and how to build.

The plain-language rules above (no em dashes, no purpose clauses) still apply. Docs for agents (`docs/`, this file)
stay as they are.

## Controller facts

These come from the owner and decide the input design (docs/plan.md, "Input").

- The speedrun uses every button, the D-pad and the L and Z buttons most of all.
- The analog stick is almost never used. Pause (Start) is rarely used.
- The four C buttons are never pressed all at the same time.
- Ideas the owner suggested: stick directions combined with the D-pad, Start combined with A, B, Z or a C button,
  and more tools on a controller in port 2.
- Any practice input must not be triggered by normal speedrun play.
- The practice menu opens and closes with all four C buttons held together (chosen by the owner).
- The ball controls are always Easy (the stick is not reversed while Glover stands on the ball), whatever the
  difficulty. A menu setting turns on the Normal (reversed) controls (docs/plan.md, "Ball controls").

## Everyday work

```sh
make setup BASEROM=path/to/glover.z64   # once; then install gcc-mips-linux-gnu (docs/build.md)
make                                     # build/glover-practice.z64, checked by tools/checklayout.py
make patch                               # the BPS patch and the patch page
make release                             # only for a release, on a tagged commit (docs/release.md)
make test                                # emulator tests; tests/run.py --quick skips the 7-minute one
```

The emulator tests need `gloveworld/tools/emu/setup.sh` once, and `Xvfb :99` running. A new feature needs a test
in `tests/` where it can be automated, and `faithful.py` must still pass with the features off.

A new feature gets its entry on the wiki page of its menu (`wiki/`), with its name as shown, what it does, its
choices and its warnings.

| What | Where |
| --- | --- |
| How the build, the layout, the boot and the hooks work | [docs/build.md](docs/build.md) |
| The version and the release steps | [docs/release.md](docs/release.md) |
| The harness, the tests, the faithfulness results | [docs/testing.md](docs/testing.md) |
| Facts about the game learned here | [docs/game.md](docs/game.md) |
| Changes that would help in gloveworld, errors found in it | [docs/gloveworld.md](docs/gloveworld.md) |
| The plan and the milestones | [docs/plan.md](docs/plan.md) |
| Ideas that need the owner | [docs/decisions.md](docs/decisions.md) |

Keep [docs/plan.md](docs/plan.md) up to date: mark milestones done, and correct what turned out different.
