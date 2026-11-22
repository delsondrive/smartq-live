# The window's title bar, merged into the top bar

## Why

On Windows 11 the main window has the standard light title strip above the app's dark top bar. It looks dated. The user
chose to merge the title bar into the app's own 62 px top bar (like VS Code or Spotify) rather than only colour the strip.

## Approaches

1. **Colour the native title bar** (DWM caption colour, dark mode). Half an hour, keeps everything native, but the
   separate strip stays. Not chosen for the main window; used for the start windows (below).
2. **Merge it into the top bar** (chosen). The window keeps its native frame (resize edges, rounded corners, shadow,
   snap), but the caption is removed and the top bar answers Windows' hit tests: empty parts of the bar are the caption,
   and the app's own minimise, maximise and close buttons are Windows' caption buttons, so Windows 11's snap layouts show
   on hovering maximise.
3. **A JUCE-drawn frame** (`setUsingNativeTitleBar(false)`). JUCE 9 does merge the caption natively for a window without
   a native title bar: its own hit test answers Windows with the caption and the window buttons
   (`Component::findControlAtPoint`, so snap layouts show over maximise), and Windows drags and snaps it. Not chosen:
   it gives up Windows' own frame, so the shadow, the rounded corners and the resize edges become JUCE's to draw and
   answer. With the native frame kept (approach 2), JUCE leaves `WM_NCCALCSIZE` and `WM_NCHITTEST` to Windows, and a
   small hook on the window procedure removes the caption and answers for the top bar, while Windows keeps its own
   shadow, rounded corners and resize edges, and its snap layouts on hovering maximise.

## Goals

- The main window shows no separate title strip. The top bar is the title bar, in every page.
- Minimise, maximise/restore and close sit at the far right of the top bar, after the EDIT/SHOW switch, drawn in the
  app's style: 46 px wide each, the bar's full height, thin line glyphs in `--mid`, a faint `--field` hover; close
  turns danger red with a white glyph on hover, as in Windows. The maximise glyph is a restore glyph while maximised.
- Edit mode: dragging an empty part of the top bar moves the window through Windows itself (edge snapping, Win+arrows,
  Aero shake keep working); double-clicking it maximises or restores. "Empty" means no tab, button, pill, chip,
  transport button, the logo (it opens About) or the EDIT/SHOW switch, and nothing over the bar (a dialog and its veil,
  which in a short window reach into the bar, are never the caption).
- Hovering the maximise button shows Windows 11's snap layouts.
- The window keeps its native resize edges (including a thin band along the top edge when not maximised), rounded
  corners and shadow. Maximised, nothing is cut off the top or the sides.
- The band's name stays the window's title (taskbar, Alt+Tab).

## Stage safety

- Show mode already takes the whole screen (kiosk) when allowed: then there is no title bar and no window buttons, as
  today.
- Show mode without kiosk: the window buttons show, but the top bar never moves, maximises or restores the window: no
  caption drag, no double-click. Every empty part of the bar is plain client area.
- Close keeps today's rule: refused with "Stop the song before closing Smart Q Live." while a song plays in Show mode.
- In the app as it is, Show mode is always kiosk: nothing turns kiosk off (no setting, no option). "Show mode without
  kiosk" above is reached only by the tests, and two things in it are left for when a kiosk-off setting is added: the 4 px
  band along the top edge of a restored window still resizes it in Show mode (a press on the very top of a tab, the switch
  or ▶ there starts a resize), unreachable today since a kiosk window has no frame (to close then: pass Show mode into
  the band's test, so Show mode has no band); and the Show bar is 138 px narrower with the buttons, so with four pills at
  1100 px the transport's "later song" button can run under the first pill (to check then: the transport against the
  pills).

## Non-goals

- The Mac (done with the Mac build: native window controls at the top bar's left).
- The start windows (Welcome, New computer): they keep a native title bar, painted in the app's colours (dark mode and
  the caption colour `theme::bg1` on Windows 11; dark mode only on Windows 10).
- Dialogs.

## Constraints

- Windows only code behind `JUCE_WINDOWS`; other platforms build unchanged.
- The JUCE peer can be recreated (kiosk on and off): the frame must follow the window's current HWND.
- The top bar's layout must still fit at the smallest window (1100 px wide) with the buttons added; pills already shrink.
- Screenshots (`--screenshots`) draw the buttons, as the window shows them in Edit mode; Show-mode screens draw none, as
  Show mode is full screen on stage.

## Left to the builder

- Exact glyph drawing, the hover and pressed tints, and how the Windows layer reaches the window procedure.
