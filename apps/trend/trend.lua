-- trend: a sparkline, a stacked sparkline and a pie, all drawn by the system.
--   run trend
--
-- **A chart is the shape of a table of numbers and not a drawing**: the app says `samples` or `slices` and the kernel
-- puts the bars where they fit, against the peak of the window and with the tones it can say on sixteen levels. Not
-- a pixel is painted here (docs/30, docs/90).
--
-- **The middle chart has two series**: `samples` is the part at the bottom and `top` the part above it, and because
-- the two are parts of one total the second one is drawn as an empty frame and not as another colour —this glass has
-- no colours to spend and the ink has to say both things (docs/90).
--
-- **The pie walks its own tones**: the app says the darkest one and the other three are four steps further on, so
-- the legend this page does not draw could say the same numbers.
local caps = sys.caps()
if not caps.display then
  console.print('trend: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Trend')

local page = ui.panel(view.width, view.height, 'vertical')
page:place(0, 0)
page.padding = 16
page.gap = 10

local heading = ui.label(10, 24)
heading.text = 'Trend'
heading.scale = 3
page:add(heading)

-- **A window of samples, the oldest on the left**: the peak is 41 and the chart scales to it.
local caption = ui.label(10, 20)
caption.text = 'Queue length, last twenty turns'
caption.scale = 2
page:add(caption)
local spark = ui.sparkline(10, 70)
spark.samples = { 3, 5, 4, 8, 6, 9, 7, 12, 10, 14, 11, 16, 13, 19, 15, 22, 18, 26, 23, 41 }
page:add(spark)

-- **The same chart with two series**: awake on the bottom and asleep on top, which is an interval split in two.
local stacked_caption = ui.label(10, 20)
stacked_caption.text = 'Awake (solid) and asleep (outline)'
stacked_caption.scale = 2
page:add(stacked_caption)
local stacked = ui.sparkline(10, 70)
stacked.samples = { 4, 6, 3, 8, 5, 9, 6, 7, 4, 8, 5, 6 }
stacked.top = { 2, 1, 4, 1, 3, 1, 2, 3, 4, 1, 3, 4 }
page:add(stacked)

-- **The pie takes the room that is left**, and its three tones are the darkest one and the two after it.
local pie_caption = ui.label(10, 20)
pie_caption.text = 'Where the time went'
pie_caption.scale = 2
page:add(pie_caption)
local pie = ui.pie(10, 10)
pie.weight = 1
pie.slices = { 5, 3, 2 }
page:add(pie)

ui.present('gc16')
console.print('trend: three charts of a table of numbers; the pie is the first tone and the three after it')

-- The page stays while the app lives: a chart is read, not pressed.
while true do
  sys.delay(500):await()
end
