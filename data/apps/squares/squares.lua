-- squares: a filled black square at a random place, painted, then wiped, over and over.
--   run squares
--
-- It is the smallest app that shows what the damage is for. Each square is **two draws and two
-- presents**: one that puts ink on the glass and one that takes it away by drawing white on top of it.
-- Neither of them costs a page —**nothing is ever repainted as a whole**— and only the region of the
-- square travels and gets painted (docs/30, docs/50).
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
local kDrawQuality = 'du'
local kEraseQuality = 'gc16'

local painted = 0
while true do
  local size = math.random(kSmallest, kLargest)
  -- Anywhere the whole square fits, so nothing is cut at the edge and the number of visible squares is
  -- exactly the number of presents.
  local x = math.random(0, view.width - size)
  local y = math.random(0, view.height - size)

  local ok, err = ui.rect(x, y, size, size, 0, kDrawQuality)
  if not ok then
    console.print('the view refused the square: ' .. tostring(err and err.code))
    return
  end
  -- The first square says everything the system knows: the region it recorded and what the panel
  -- answered. If a paint is dropped, this is the line that says why (docs/95).
  if painted == 0 then
    console.print(string.format('square at %d,%d %dx%d (%s), %d region(s) with damage', x, y, size,
      size, kDrawQuality, ui.damage()))
  end
  local asked, why = ui.present()
  if not asked then
    console.print('the panel did not take the paint: ' .. tostring(why and why.reason))
    return
  end
  if painted == 0 then
    console.print(string.format('painted, %d region(s) left, idle says %s', ui.damage(),
      tostring(ui.idle())))
  end
  sys.delay(kSettleMs):await()

  -- Erasing is drawing white over the same rectangle: there is no `erase`, because a canvas does not
  -- know what is behind it — and there is nothing behind it. **With `gc16`**, which is the quality that
  -- leaves no history: the ink of the previous square is taken away, not painted over.
  ui.rect(x, y, size, size, 15, kEraseQuality)
  ui.present()
  sys.delay(kSettleMs):await()

  painted = painted + 1
  if painted % 25 == 0 then
    console.print(string.format('%d squares, two regions of their own each', painted))
  end
end
