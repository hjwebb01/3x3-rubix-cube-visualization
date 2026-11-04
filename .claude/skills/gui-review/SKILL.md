---
name: gui-review
description: Adversarial review of the rubiks GUI layout and interaction, then fix what matters. Use for "review the gui", "adversarial review", "check the interface".
---
1. Build, then take shots: `./build/rubik_gui --screenshot /tmp/a.png 1360 820`, `... 960 600`, plus `--faces` at both sizes.
2. Look at the shots and the running window. Check clipping, overlap, empty space, keyboard reachability, error text truncation.
3. Report findings ranked must / should / nit with file:line.
4. Fix must + should only. Re-shoot at both sizes and compare. Run ctest.
