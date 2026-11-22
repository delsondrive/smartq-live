# Title bar: decisions made while building

The window's separate Windows title strip is gone. The app's own top bar is now the title bar: minimise, maximise and
close sit at its far right, and in Edit mode you drag an empty part of it to move the window. Windows still does the
moving, snapping, snap layouts (hover over maximise), resizing, rounded corners and shadow. These are the calls I made
for you. Each says what it costs if it was wrong.

## Calls

- **Show mode is untouched on stage.** It already fills the whole screen with no title bar and no window buttons, and
  closing mid-song is still refused. Cost if wrong: none.
- **The start windows (Welcome, New computer) keep a Windows title strip, painted dark** to match the app. Cost if
  wrong: those two windows look slightly different from the main one.
- **A dialog that reaches over the top bar is never a handle** for moving the window. Cost if wrong: none.
- **The Show-mode screenshots draw no window buttons,** since on stage the drummer never sees them. Cost if wrong: none.
- **Left for a later "Show mode in a window" setting** (nothing turns full screen off today, so neither can happen): the
  thin resize band at the very top edge would still work in Show mode, and with four status pills at the narrowest
  width the transport's last button could run under the first pill. Both are noted in the design. Cost if wrong: none
  today.
- **The maximise button's picture changes only when the window is resized.** Clicking it always does the right thing.
  Cost if wrong: in a rare case the button shows ☐ when it should show ❐ until the window is next resized.
- **One screenshot test checks the top-right corner by colour.** A later restyle of the EDIT/SHOW switch could make it
  fail for a confusing reason. Cost if wrong: a test to adjust.

## The reviews

Each of the two tasks was reviewed, then the whole change, then its fixes. Nothing critical was found. The important
ones fixed: the button hover could be wiped by the window's own mouse handling; the hand-check list asked for steps that
can't be done. 834 tests pass.

## Things only you can check

They are in `docs/acceptance.md`, section 31. The ones that matter most:

- drag the bar to move the window, and to a screen edge to snap it;
- hover over maximise for Windows 11's snap layouts;
- maximise: nothing cut off at the top, and an auto-hiding taskbar still slides up;
- display scaling at 125 % and 150 %;
- Alt+F4 mid-song in Show mode is refused, and closing from Show mode doesn't flash a light strip.
