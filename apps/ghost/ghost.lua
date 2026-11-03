-- ghost: the page that stays (docs/85, docs/95 E-7).
--
-- An e-ink panel does not switch pixels, it moves pigment. What was there does **not** go away in one
-- paint: a quick paint leaves a fraction of it, and the fractions of eight paints in a row are a page
-- nobody can read. Only the qualities that clean take the page back. This app is that, in eight passes
-- and one clean, and it says how long each paint took so the number can be read next to the picture.
--
--   run ghost          a full cycle, and it ends by itself so a script can run it
--
-- What to look at on the board: the block in the middle keeps the shape of every pass before it, the
-- counter leaves the numbers of the passes it painted over, and the `gc16` at the end takes all of it.
-- On the bench the same page is in the window (`--gui`) and the same numbers come out of `stats`.
local caps = sys.caps()
if not caps.display then
  console.print('this board has no panel')
  return
end

local view = ui.caps()
-- The two qualities this app is about: the quick one that leaves a trace and the slow one that cleans
-- (docs/50, docs/30).
local kQuick = 'du'
local kFine = 'gc16'
-- Eight passes: enough that the traces are unmistakable, and short enough to watch happen.
local kPasses = 8

ui.title('ghost')

-- **What one paint costs**, counted the way the panel app counts it: from before the request to the
-- moment the glass says it is over, with no sleep of the app's own in between (docs/30, docs/95).
local function paint(mode)
  local regions = ui.damage()
  local started = sys.time_ms()
  local ok, err = ui.present(mode)
  if not ok then
    console.print('the panel did not take the paint: ' .. tostring(err and err.code))
    return -1, 0
  end
  while not ui.idle() do
    sys.delay(10):await()
  end
  return sys.time_ms() - started, regions
end

-- A page that is clean before anything is drawn: whatever is on the glass now is somebody else's page,
-- and a demonstration of ghosting that starts with a ghost of an old app proves nothing (docs/85).
ui.clear(15, kFine)
ui.frame(10, 10, view.width - 20, view.height - 20, 0, kFine)
ui.text(24, 24, 'ghost: the page that stays', 2, 0, kFine)
ui.text(24, 66, 'eight paints of du, then one gc16', 1, 0, kFine)
-- The block that never moves and never changes: its edges are where a trace shows first.
ui.rect(60, 140, 220, 180, 0, kFine)
ui.text(24, 340, 'the block, the counter and the bar are painted again and again', 1, 0, kFine)
local first, regions = paint(kFine)
console.print(string.format('the clean page is on the glass: %d ms, %d region(s)', first, regions))

-- **The passes.** Each one paints the block white or black the other way round, moves a bar down the
-- page, and draws the pass number with the quick quality over the one before. The ink that was there
-- stays at a fraction of its level and the fraction adds up, which is what a person sees as ghosting.
for pass = 1, kPasses do
  ui.rect(60, 140, 220, 180, (pass % 2 == 0) and 0 or 15, kQuick)
  ui.rect(320, 360 + (pass - 1) * 22, 160, 14, 0, kQuick)
  -- The counter goes in the same place every time: the numbers of the passes before are still there,
  -- faintly, and that is the whole point of the app.
  ui.rect(320, 150, 180, 56, 15, kQuick)
  ui.text(330, 160, 'pass ' .. tostring(pass), 3, 0, kQuick)
  local took = paint(kQuick)
  console.print(string.format('pass %d of %d: the ink took %d ms', pass, kPasses, took))
end
console.print('look at the block: every pass before it is still there, and the counter too')

-- **And the page back.** A `gc16` of the whole view is the only thing that takes the traces away, and it
-- is what makes a page readable again (docs/30, docs/50).
ui.clear(15, kFine)
ui.text(24, 24, 'ghost: cleaned with gc16', 2, 0, kFine)
ui.text(24, 66, 'the traces of eight quick paints are gone', 1, 0, kFine)
local cleaned, cleaned_regions = paint(kFine)
console.print(string.format('the page was cleaned with gc16: %d ms, %d region(s)', cleaned,
  cleaned_regions))
console.print('run it again to watch it come back, and ask `stats` what the paints cost the system')
