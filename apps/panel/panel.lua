-- panel: draws a test page on the app's own canvas and asks the system to put it on the glass.
--
-- It is the first thing that can be seen on the e-ink panel, and it exists to answer four questions
-- with the board in front of you (docs/95, E-7):
--
--   1. does the image arrive at all, and the right way up?
--   2. do the sixteen levels come out **black at 0 and white at 15**, or the other way round?
--   3. how long does the paint take, and does `ui.idle()` say the truth about it?
--   4. does a present that carries **two qualities** paint twice, each region with its own?
--
-- **The app draws on its own view, not on the panel.** `ui.clear`, `ui.rect` and `ui.pixel` write to a
-- canvas the system made for this app and record **which region was touched and how good it has to
-- look**; `ui.present` composes that damage onto the board's buffer and asks for each region to be
-- painted with its own quality. Nothing of the system paints, and the page stays on the glass when the
-- app leaves (docs/30, docs/50).
local caps = sys.caps()
if not caps.display then
  console.print('this board has no panel')
  return
end

local view = ui.caps()
console.print(string.format('view %d x %d at %d,%d, %d levels', view.width, view.height, view.x,
  view.y, view.levels))

-- A white page, drawn with the quality it deserves: the frame and the squares are quick changes
-- (`du`), and the ladder of sixteen levels is the thing that has to look right (`gc16`). The two names
-- are written once, because the console says which ones the page used and a page that says one thing and
-- paints another is how the first run of this app lied (docs/95, E-7).
--
-- **Not `du4`, and that is a measurement and not a taste**: on the board of 1/11 `du4` and `a2`
-- answered `true` and left the glass as it was (docs/95, E-7), so a page drawn with `du4` is a page that
-- may not arrive at all. `du` paints —with the trace it leaves, which this page does not care about
-- because it is drawn once and read once—.
local kQuick = 'du'
local kFine = 'gc16'
ui.clear(15, kQuick)
ui.rect(0, 0, view.width, 2, 0, kQuick)
ui.rect(0, view.height - 2, view.width, 2, 0, kQuick)
ui.rect(0, 0, 2, view.height, 0, kQuick)
ui.rect(view.width - 2, 0, 2, view.height, 0, kQuick)

-- The ladder is what tells the levels apart at a glance: from black on the left to white on the right,
-- in steps. It is drawn with `gc16` because it is the one region whose contrast matters.
local bar = math.floor(view.width / 16)
for level = 0, 15 do
  ui.rect(level * bar, 40, bar, 120, level, kFine)
end
for i = 0, 4 do
  ui.rect(40 + i * 60, 220, 40, 40, 0, kQuick)
end

-- What the system is about to pay for: the regions it merged and the qualities it found.
console.print(string.format('%d region(s) with damage, %s and %s', ui.damage(), kQuick, kFine))

-- **The clock starts before the paint is asked for, and it is stopped by `ui.idle()` and by nothing
-- else.** `ui.present` returns at once —the ink takes its time and the system does not wait for it—, so
-- the number below is the wait for the glass and not a sleep of the app's own. A sleep here would be a
-- number dressed up as a measurement, which is what this app printed until the 0.19.2 (a flat 120 ms).
local started = sys.time_ms()
local ok, err = ui.present()
if not ok then
  console.print('the panel did not take the paint: ' .. tostring(err and err.code))
  return
end
-- The first question is safe to ask immediately: the driver does not say "over" before the engine has
-- had the time to mark itself busy (docs/50), so `idle()` cannot answer while the ink has not started.
while not ui.idle() do
  sys.delay(10):await()
end
console.print(string.format('the ink took %d ms (asked with a poll of 10 ms)', sys.time_ms() - started))
console.print('now try the shell: the page stays on the glass until something else paints it')

-- A new size for the view —a rotation of the glass, one day— arrives here: the system has already made
-- the canvas and this is the app being asked to draw it again (docs/30).
function on_resize(width, height)
  console.print(string.format('view is now %d x %d', width, height))
  ui.clear(15, kQuick)
  ui.rect(0, 0, width, 2, 0, kQuick)
  ui.present()
end
