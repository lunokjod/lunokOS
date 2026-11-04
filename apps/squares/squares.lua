-- squares: a filled black square at a random place, painted, then wiped, over and over.
--   run squares
--
-- It is the smallest app that shows what the damage is for. Each square is **two draws and two
-- presents**: one that puts ink on the glass and one that takes it away by drawing white on top of it.
-- Neither of them costs a page —**nothing is ever repainted as a whole**— and only the region of the
-- square travels and gets painted (docs/30, docs/50).
--
-- **The trace stays, and that is the demo** (4/11): the square is erased with `du`, which lands close
-- to white and not on it, so what is left is a block of light grey. Erasing every square with `gc16`
-- —what this app did until today— left the page perfectly white, which is the one thing an app about
-- damage must not do: `gc16` takes the region back to the level and the dirt with it, and a page of
-- squares ended up cleaner than it started. One square in `kCleanEvery` is erased with `gc16` all the
-- same, so the two qualities can be seen side by side (measured on the board, docs/95).
--
-- Leave it with three ESC, like any app.
local caps = sys.caps()
if not caps.display then
  console.print('this board has no panel')
  return
end

-- Who is answering, first of all: an app that draws nothing and a firmware that is not the one you
-- flashed look the same from the outside (docs/95).
console.print('lunokOS ' .. sys.version .. ' on ' .. sys.board)

local view = ui.caps()
console.print(string.format('view %d x %d at %d,%d, %d levels', view.width, view.height, view.x,
  view.y, view.levels))

math.randomseed(sys.time_ms() + 1)

local kSmallest = 24
local kLargest = 96
local kSettleMs = 260
-- **Two qualities, and for a measured reason.** `du` paints quickly but **does not clean**: a black
-- square erased with `du` leaves its grey trace, and after a while the page has darker areas and the
-- edges of the squares before it (measured on the board, 1/11). `gc16` does bring the white back, and
-- over **the region of the square** — which is what this app does, not a page clean (docs/50, docs/95).
-- So the erase is `du` —the trace is the app's whole point— and one square in `kCleanEvery` is erased
-- with `gc16` instead, which is the same white rectangle with the quality that leaves no history: what
-- a real system does now and then, in the region it dirtied and not on the page.
local kDrawQuality = 'du'
local kEraseQuality = 'du'
local kCleanQuality = 'gc16'
local kCleanEvery = 20

local painted = 0
while true do
  local size = math.random(kSmallest, kLargest)
  -- Anywhere the whole square fits, so nothing is cut at the edge and the number of visible squares is
  -- exactly the number of presents.
  local x = math.random(0, view.width - size)
  local y = math.random(0, view.height - size)

  -- The first square says everything the system knows: the region it recorded, what the panel answered
  -- **and how long the ink took**, counted from before the draw until `ui.idle()` says it is over, with
  -- no sleep of the app's own in between (docs/95, E-7). One quality per present, which is what makes
  -- this the app that can tell what each one costs.
  local first = (painted == 0)
  local ok, err = ui.rect(x, y, size, size, 0, kDrawQuality)
  if not ok then
    console.print('the view refused the square: ' .. tostring(err and err.code))
    return
  end
  if first then
    console.print(string.format('square at %d,%d %dx%d (%s), %d region(s) with damage', x, y, size,
      size, kDrawQuality, ui.damage()))
  end
  -- The clock starts **after the app has drawn and before the panel is asked**, and it is the poll of
  -- `ui.idle()` that stops it: the number is the glass and nothing else (docs/95, E-7).
  local started = sys.time_ms()
  local asked, why = ui.present()
  if not asked then
    console.print('the panel did not take the paint: ' .. tostring(why and why.reason))
    return
  end
  if first then
    while not ui.idle() do sys.delay(10):await() end
    console.print(string.format('painted with %s in %d ms', kDrawQuality, sys.time_ms() - started))
  end
  sys.delay(kSettleMs):await()

  -- Erasing is drawing white over the same rectangle: there is no `erase`, because a canvas does not
  -- know what is behind it — and there is nothing behind it. **With `du`**, which paints white and lands
  -- near it: what is left is the light grey trace that makes a page of squares a page of damage. Every
  -- `kCleanEvery` squares the same rectangle goes with `gc16`, the quality that leaves no history, and
  -- that region does come back white (docs/50, docs/95).
  local clean = (painted > 0) and (painted % kCleanEvery == 0)
  local erase_quality = clean and kCleanQuality or kEraseQuality
  ui.rect(x, y, size, size, 15, erase_quality)
  started = sys.time_ms()
  ui.present()
  if first or clean then
    while not ui.idle() do sys.delay(10):await() end
    console.print(string.format('erased with %s in %d ms', erase_quality, sys.time_ms() - started))
  end
  sys.delay(kSettleMs):await()

  painted = painted + 1
  if painted % 25 == 0 then
    console.print(string.format('%d squares, two regions of their own each', painted))
  end
end
