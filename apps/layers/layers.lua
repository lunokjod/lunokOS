-- layers: controls inside controls, and a view that redraws only what changed.
--   run layers
--
-- **A control is a canvas with children.** One father, the children in the order they were added, and
-- that order is the order of the layers. Drawing a control is `draw()`: it paints its own canvas with the
-- `on_draw` the app gave it and **then asks every child**, in order (docs/90).
--
-- **And a control with nothing changed is not drawn at all**: its `on_draw` is not called, no pixel moves,
-- nothing reaches the glass. The counter below is the proof: two presents in a row draw once.
--
-- Leave it with three ESC, like any app.
local caps = sys.caps()
if not caps.display then
  -- The path an app takes on a board with no glass: the call says so and the app ends clean.
  console.print('layers: no view')
  return
end

console.print('lunokOS ' .. sys.version .. ' on ' .. sys.board)
local view = ui.caps()
console.print(string.format('layers: view %d x %d at %d,%d', view.width, view.height, view.x,
  view.y))

local draws = 0

-- The panel: a border with a title, and nothing else. The children do the rest.
local panel = ui.canvas(200, 150)
panel.on_draw = function(self)
  draws = draws + 1
  self:clear(15, 'gc16')
  self:frame(0, 0, 200, 150, 0, 'gc16')
  self:text(10, 8, 'PANEL', 2, 0, 'gc16')
  self:line(10, 32, 189, 32, 8, 'gc16')
end
panel:place(math.floor((view.width - 200) / 2), 40)

-- A button inside the panel: black with white letters. Its place is in the panel's pixels, not the
-- page's, which is what nesting means.
local button = ui.canvas(120, 40)
button.on_draw = function(self)
  self:clear(0, 'gc16')
  self:text(14, 12, 'PUSH', 2, 15, 'gc16')
end
button:place(20, 10)
-- **A control without a view**: a box that holds the button and paints nothing at all. It costs no pixels,
-- and the button lands where the box and the button say (docs/90).
local box = ui.control(160, 60)
box:place(20, 60)
box:add(button)
panel:add(box)

-- And a child of the button, to show that the tree goes as deep as the app wants.
local dot = ui.canvas(16, 16)
dot.on_draw = function(self)
  self:disc(8, 8, 6, 15, 'gc16')
end
dot:place(140, 20)
button:add(dot)

-- **One present**: the panel paints, then the button, then the dot, and the page goes to the glass.
ui.present()
while not ui.idle() do sys.delay(50):await() end
console.print(string.format('layers: %d draw of the panel, and the page is on the glass', draws))

-- **A second present with nothing changed**: no `on_draw`, no paint. That is the whole point of the
-- flag, and the number has to stay where it was.
ui.present()
while not ui.idle() do sys.delay(50):await() end
console.print(string.format('layers: %d draw of the panel after a present with nothing new', draws))

-- Now something changes: the button moves, and its father has to paint over what it leaves behind.
button:place(30, 20)  -- inside the box, so the page clips nothing
ui.present()
while not ui.idle() do sys.delay(50):await() end
console.print(string.format('layers: %d draw of the panel after the button moved', draws))
console.print(string.format('layers: button is %s, its father is slot %s', tostring(button),
  tostring(button:parent())))
console.print(string.format('layers: the box that holds it is %s and has no view of its own',
  tostring(box)))

-- **A control that goes away**: `release` asks, the button paints over the hole on the next present, and the
-- buffer is wiped and the slot given back after it. The object is dead from the moment it was asked.
dot:release()
ui.present()
while not ui.idle() do sys.delay(50):await() end
console.print('layers: after the dot went away, ' .. tostring(dot))
