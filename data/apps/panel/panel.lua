-- panel: draws a test page and asks the glass to paint it.
--
-- It is the first thing that can be seen on the e-ink panel, and it exists to answer three questions
-- with the board in front of you (docs/95, E-7):
--
--   1. does the image arrive at all, and the right way up?
--   2. do the sixteen levels come out **black at 0 and white at 15**, or the other way round?
--   3. how long does a `gc16` take, and does `display.idle()` say the truth about it?
--
-- **Nothing of the system paints.** This app decides what is on the glass, with which mode and when:
-- when it leaves, the page stays there and nothing repaints it (docs/30, docs/50).
local caps = sys.caps()
if not caps.display then
  console.print('this board has no panel')
  return
end

local panel = display.caps()
console.print(string.format('panel %d x %d, %d levels', panel.width, panel.height, panel.levels))

-- A white page with a black frame, a ladder of the sixteen levels and five black squares. The ladder is
-- what tells the levels apart at a glance: from black on the left to white on the right, in steps.
display.clear(15)
display.rect(0, 0, panel.width, 2, 0)
display.rect(0, panel.height - 2, panel.width, 2, 0)
display.rect(0, 0, 2, panel.height, 0)
display.rect(panel.width - 2, 0, 2, panel.height, 0)

local bar = math.floor(panel.width / 16)
for level = 0, 15 do
  display.rect(level * bar, 40, bar, 120, level)
end
for i = 0, 4 do
  display.rect(40 + i * 60, 220, 40, 40, 0)
end

-- `gc16` and not `du`: this is a page nobody is going to look at twice in a row, and it is the mode
-- that shows what the glass can really do (docs/50).
local ok, err = display.flush('gc16')
if not ok then
  console.print('the panel did not take the paint: ' .. tostring(err and err.code))
  return
end
console.print('paint asked; waiting for the glass')

-- The ink takes its time and the system does not wait for it: the app asks, in its own loop, whether
-- the panel has finished (docs/30).
local started = sys.time_ms()
while not display.idle() do
  sys.delay(100):await()
end
console.print(string.format('done in about %d ms', sys.time_ms() - started))
console.print('now try the shell: the page stays on the glass until something else paints it')
