-- grid: a menu of things, picked with the eye.
--   run grid
--
-- **The cells are the system's and the list is this app's**: the app says a name and a short mark per cell and not one pixel
-- of the grid is painted here (docs/30, docs/90).
--
-- **The page follows the cursor**: the app hands over the whole list and never turns a page; the arrows walk the cells and the
-- grid decides which page is on the glass and says it in its footer, so a menu of forty things is the same code as a menu of
-- four.
--
-- **The mark is what is recognised at a glance and the name is what confirms it**: that is why this is a grid and not a list,
-- and why the cell that is chosen is inverted while the one the dial is on wears the ring. The name stays in the ink even on a
-- chosen cell, because it sits on the paper of the page and not inside the box of the mark.
--
-- It stops by itself after forty seconds, so a script can run it.
local caps = sys.caps()
if not caps.display then
  console.print('grid: this board has no glass')
  return
end

local view = ui.caps()

ui.title('Grid')

local status = ui.label(view.width - 48, 26)
status.text = 'nothing chosen yet'
status.scale = 2
status.color = 8
status:place(24, 40)

-- Twelve cells of a menu that does not fit in one page at the smallest size, which is what the paging is for.
local things = {
  {label = 'Notes', mark = 'N'}, {label = 'Clock', mark = 'C'}, {label = 'Files', mark = 'F'},
  {label = 'Paint', mark = 'P'}, {label = 'Music', mark = 'M'}, {label = 'Radio', mark = 'R'},
  {label = 'Codes', mark = 'S'}, {label = 'Maps', mark = 'G'}, {label = 'Photos', mark = 'H'},
  {label = 'Mail', mark = 'E'}, {label = 'Weather', mark = 'W'}, {label = 'Settings', mark = 'T'},
}

local grid = ui.icon_grid(view.width - 40, view.height - 220)
grid:place(20, 80)
grid.columns = 4
grid.marked = 1
grid.items = things
grid.on_pick = function(self, index)
  self.marked = index
  status.text = 'you chose ' .. self.items[index].label
  console.print('grid: ' .. status.text)
  ui.present('gld16')
end

ui.present('gld16')
console.print('grid: the arrows walk the cells, the page follows, and the centre opens one')

-- **The page stays while there is somebody on it**: a demo that closes after forty seconds closes in the middle of
-- the one thing it teaches, and the shell takes every key after that. The way out is the shell's, and what the
-- person chose is said there (docs/30, docs/90).
function on_close()
  console.print('grid: closed on cell ' .. tostring(grid.index))
end

while true do
  sys.delay(100):await()
end
