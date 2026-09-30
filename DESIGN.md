# TrevOS design system

One small physical surface, three clear jobs. The OS frames the apps. Each face makes its main action visible and fits the circular glass.

## Identity and tokens

The executable source of truth is [trevos_theme.h](os/trevos/include/trevos_theme.h). Use semantic tokens and font roles instead of new local colors or fonts.

| Role | Token | Value |
|---|---|---|
| Paper ground | `TT_PAPER` | `#F2EFE6` |
| Light paper | `TT_PAPER_ALT` | `#FBFAF4` |
| Main text | `TT_INK` | `#1C1B19` |
| Structural accent | `TT_SLATE` | `#3F5266` |
| Secondary text | `TT_DESC` | `#5A5953` |
| Pomodoist accent | `TT_CORAL` | `#E94F35` |
| Ring track | `TT_RING_TRACK` | `#E7E2D5` |
| Break ground | `TT_CARBON` | `#131210` |
| Break accent | `TT_AMBER` | `#E0A24E` |

TrevOS uses paper, ink and slate. Pomodoist is a warm lit page with coral reserved for its emphasis; breaks invert to carbon and amber. Cal uses event-colored surfaces and an ink-on-white reading hierarchy. The OS does not impose the Pomodoist accent on Cal.

IBM Plex Sans carries prose and IBM Plex Mono carries numbers and labels. Countdown figures are monospaced so ticking does not move the line. The round scale uses a 72 px numeral, 22 px title, 14 px body and 13 px status/label role; see the header for smaller roles and the alternate geometry scale. The bitmap fonts retain their [OFL license](LICENSES/IBMPlex-OFL.txt).

## Round layout and controls

The target is 360 × 360. Keep text within the safe radius of 168 px, narrowing each row as it approaches the top or bottom chord. Use an 8 px spacing rhythm. Text fits, shrinks or ellipsizes; it never overlaps neighboring content or disappears behind the bezel.

Dial faces have one numeral hero, at least 2.5 times the next-largest text. Reading faces such as Cal, Settings and task detail use area and grouping for hierarchy. Draw the control where its tap zone is. The bottom chord holds two visible 96 × 44 px pills with half-width input zones. Primary pills are filled; secondary pills use a 2 px outline. A third action belongs to the wheel or another state.

Use `TT_DESC` for new secondary text on paper. The older muted tone does not meet a 4.5:1 text contrast target. Do not indicate a state through color alone; pair the appearance with a label, shape, motion or haptic cue.

## Launcher and input

The ring is centered at 180,180, with an outer band from radius 126 to 172 and an inner disc of radius 118. Eight visual positions use 45 degree segments and 2 degree gaps. Settings sits at six o'clock. Selected segments are slate with paper icons; empty positions are small dots.

The wheel selects and adjusts; the touchscreen commits. The hardware wheel does not have a click action. A detent at a list boundary causes neither movement nor a false tick. The first input while dark wakes the screen without activating the underlying control. The application definition contract and board input routing remain the implementation authority.

## Motion and feedback

Motion follows the wheel from the current visible value, retargeting a running animation rather than queueing another one. Default per-detent timing is 150 ms ease-out. Use layer-free position, translation, arc and color operations; keep the `TT_NO_LAYER_FX` constraint.

- Cal: event group travels 16 px from the direction of the detent.
- Launcher: selected arc sweeps one 45 degree slot.
- Settings: rows travel 56 px through a fixed pill; the section boundary has a distinct transition.
- Focus length: readout travels 8 px in 120 ms.
- Open setting: the rim arc follows its discrete value without stacking motion.

Taps take effect immediately. A new detent begins from the live animation value. A haptic tick acknowledges an actual change; confirmations use the OS feedback hook. With haptics disabled, visual feedback remains.

## Honest states and reference media

A clock not yet verified by a trusted source carries a tilde. Calendar chimes require trusted time. Missing network data, expired authorization and empty results have different meanings and should have distinct states. Never invent battery percentage or show a sync timestamp that did not happen.

Reference screens and concept renders live under [design](design/README.md). Simulator pictures demonstrate layout with synthetic data. They do not prove physical touch accuracy, endurance or battery behavior. Concepts illustrate a setting or finish, not a measured enclosure or a new shipped feature.
