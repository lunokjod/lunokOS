-- cost: what the board costs, measured where it runs (docs/85, docs/95).
--
-- The same app on the Paper and on the bench, so the numbers can be put side by side: a `stat`, a
-- listing, a read, a write, one paint of each quality, one read of the battery and one of the glass.
-- The bench is told to pay the times of the hardware —a block of the flash, a sector of the card, the
-- bus of the panel, four ADC conversions— so what comes out of the two runs is meant to be the same
-- number, and when it is not, one of the two is lying (docs/85).
--
--   run cost
--
-- A cost of a fraction of a millisecond —the gauge is about four hundred microseconds— is measured over
-- many calls and divided, because the clock of an app is in milliseconds and a single read of it would
-- be a zero (docs/30).
local kStats = 50
local kLists = 5
local kReads = 20
local kWrites = 5
local kBattery = 50
local kTouch = 50

local function line(name, count, total_ms)
  if count <= 0 then return end
  console.print(string.format('  %-26s %4d calls %9.2f ms %8.3f ms each', name, count, total_ms,
    total_ms / count))
end

-- **Who is answering**: a bench that says it is a bench, and the board it stands in for (docs/30).
console.print(string.format('cost on %s (%s)', sys.board, sys.emulator and 'bench' or 'board'))
local caps = sys.caps()
console.print(string.format('  display %s, touch %s, battery %s, card %s',
  tostring(caps.display), tostring(caps.touch), tostring(caps.battery), tostring(caps.sd)))

-- **The disk.** A `stat` of a directory that every image has, a listing of it, a file of one kilobyte
-- written and read back: one block of the flash if that is where it lives (docs/70, docs/85).
local started = sys.time_ms()
for _ = 1, kStats do
  fs.stat('/littlefs/apps')
end
line('fs.stat /littlefs/apps', kStats, sys.time_ms() - started)

started = sys.time_ms()
for _ = 1, kLists do
  fs.list('/littlefs/apps')
end
line('fs.list /littlefs/apps', kLists, sys.time_ms() - started)

local ground = fs.data()
fs.mkdir(ground)
local probe = fs.data('cost.txt')
local payload = string.rep('x', 1024)
started = sys.time_ms()
for _ = 1, kWrites do
  fs.write(probe, payload)
end
line('fs.write 1 KB', kWrites, sys.time_ms() - started)

started = sys.time_ms()
for _ = 1, kReads do
  fs.read(probe)
end
line('fs.read 1 KB', kReads, sys.time_ms() - started)

-- The space is a walk of the whole filesystem and it is the call that costs the most of this module
-- (docs/30, docs/70), so it is measured alone.
started = sys.time_ms()
fs.space('/littlefs')
line('fs.space /littlefs', 1, sys.time_ms() - started)

-- **The glass**, one paint of each quality, counted from before the request to the moment the panel says
-- it is over. The number is the transfer of the region and the ink, and the ink is the datasheet's
-- (docs/50, docs/85).
if caps.display then
  local view = ui.caps()
  local qualities = {'du', 'gc16'}
  for _, quality in ipairs(qualities) do
    ui.rect(20, 20, 200, 100, 0, quality)
    local paint_started = sys.time_ms()
    if ui.present() then
      while not ui.idle() do
        sys.delay(10):await()
      end
      console.print(string.format('  paint 200x100 %-10s %26d ms', quality, sys.time_ms() - paint_started))
    end
  end
  ui.clear(15, 'gc16')
  if ui.present() then
    while not ui.idle() do
      sys.delay(10):await()
    end
  end
  -- The view of an app, for the record: it is the frame the fingers are given in (docs/30).
  console.print(string.format('  view %dx%d at %d,%d', view.width, view.height, view.x, view.y))
else
  console.print('  paint: this board has no panel')
end

-- **The gauge**: a battery behind a divider on an ADC pin, four conversions a read (docs/50, docs/85).
if power and caps.battery then
  local sample = power.battery()
  started = sys.time_ms()
  for _ = 1, kBattery do
    power.battery()
  end
  line('power.battery', kBattery, sys.time_ms() - started)
  if sample then
    console.print(string.format('  the cell reads %d mV, %d %%%s', sample.mv, sample.pct,
      sample.charging and ', charging' or ''))
  end
else
  console.print('  power.battery: this board has no gauge')
end

-- **The glass of the fingers**: the points are the frame of the kernel's last turn and not a fresh read
-- of the chip, so this is what an app pays for asking (docs/30, docs/50).
if caps.touch then
  started = sys.time_ms()
  for _ = 1, kTouch do
    touch.read()
  end
  line('touch.read', kTouch, sys.time_ms() - started)
  console.print('  (the chip is read once a turn by the kernel: this is the frame, not the bus)')
else
  console.print('  touch.read: this board has no panel of fingers')
end

-- **And the memory**, which is where an app lives: the ceiling it was given and the peak it reached
-- (docs/75).
local heap = sys.heap()
if heap then
  local ceiling = 'no ceiling of its own'
  if heap.limit > 0 then ceiling = string.format('%d KB', heap.limit // 1024) end
  console.print(string.format('  heap: %d KB free of %s, peak %d KB used', heap.free // 1024, ceiling,
    heap.peak // 1024))
end
console.print('run the same line on the board and put the two tables side by side (docs/85)')
