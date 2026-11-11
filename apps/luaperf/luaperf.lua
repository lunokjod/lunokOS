-- luaperf: **what one instruction of Lua costs on this board, and what the turn budget is worth in milliseconds**
-- (docs/75, docs/85, docs/95). The question it answers is the one the emulator cannot: the emulator *models* the price
-- of an instruction (`--lua-cpu`, 60 ns at 160 MHz) and the model is a guess until a board says otherwise, so the
-- decision "is a turn of this many instructions a lot?" needs this number and not the model's.
--
-- **What it measures, and how.** A loop whose cost in instructions is known exactly —`for i = 1, n do k = k + 1 end`,
-- which Lua 5.4.7 compiles to `ADDI`, `MMBINI` and `FORLOOP`: **three instructions a lap**— timed with `sys.time_ms()`.
-- Nothing else is called in the middle, so the time of `L` laps is `L × 3` instructions and the turns the kernel cut
-- them into. Those turns are `L × 3 / budget`, so **one turn falls every `budget / 3` laps and the two costs cannot be
-- told apart from here**: only their sum is visible, and the sum is what matters —the price of an instruction with the
-- kernel's own turn inside it— and it is what the app says (`kTurnOverheadUs`, 850 µs in the emulator, docs/85). The
-- first run sizes the second; the second, of about half a second, is the measurement.
--
-- **The budget is the board's and it is read, not copied** (0.174.7): it is a setting —`cfg turn <n>`, and the build
-- ships twenty thousand— so it comes from `sys.turn()` and this app never disagrees with the hook that cuts the turn.

local kBudget = sys.turn()
local kInstrPerLap = 3    -- ADDI + MMBINI + FORLOOP, counted on the bytecode of this file's loop

-- **`sys.caps()` is asked once, at the top, because the first thing this file does with a page is ask it to be quiet**
-- (0.179.9): on a board with no glass there is no `ui` table at all —the build does not carry it— and a `ui.idle()` before
-- the check was an app that died on its first measurement with `attempt to index a nil value (global 'ui')`. The console is
-- where the numbers go on that board, and that is said at the end (docs/30, docs/50).
local caps = sys.caps()

-- **A loop with no call in it**: one lap is three instructions and nothing else, so a second of laps is a second of
-- instructions (a call into the system would be a call into the system, and that is another measurement).
local function spin(laps)
  local n = 0
  for i = 1, laps do n = n + 1 end
  return n
end

local function ms_of(laps)
  local started = sys.time_ms()
  spin(laps)
  return sys.time_ms() - started
end

-- **What the loop costs, the smallest of three tries and with the glass quiet before each one.** The wall time of a loop
-- is not all the loop: a turn of the kernel around it and, above all, a page of ink that the system is painting —the page
-- of the launcher this app was started from, the stripe of the bar— go into it, and a page of ink is a hundred
-- milliseconds (docs/95). A paint only ever *adds* time, never takes it away, so the smallest of three is the closest to
-- the loop, and the wait for an idle glass takes the biggest of them out of the count (found on the emulator, 9/11: the
-- same loop measured 195 and 2 085 ns a lap in two runs, the second one with the page of the boot still moving).
local function quiet()
  if not caps.display then return end
  if ui.idle() then return end
  local waited = 0
  while not ui.idle() and waited < 3000 do
    sys.delay(20):await()
    waited = waited + 20
  end
end

local function cheapest(laps, tries)
  local best = nil
  for _ = 1, tries do
    quiet()
    local ms = ms_of(laps)
    if best == nil or ms < best then best = ms end
  end
  return best
end

-- **A warm-up thrown away**: the first laps of an interpreter pay for what it has not done yet, and this is not a price.
spin(50000)

-- **The probe**: half a million laps are a few tens of milliseconds on a board of this family —enough for a clock of one
-- millisecond and short enough that a person does not wait for it— and it is what sizes the measurement.
local probe_laps = 500000
local probe_ns_lap = (cheapest(probe_laps, 3) * 1000000.0) / probe_laps
while probe_ns_lap < 40 do
  probe_laps = probe_laps * 4
  probe_ns_lap = (cheapest(probe_laps, 2) * 1000000.0) / probe_laps
end

-- **And half a second of laps**, which is many turns: the clock of a millisecond is a tenth of a per cent there, and the
-- laps are the ones the probe says fit in half a second.
local laps = math.max(1000, math.floor(500000000.0 / probe_ns_lap))
local wall_ms = cheapest(laps, 2)
local ns_lap = (wall_ms * 1000000.0) / laps
local ns_instr = ns_lap / kInstrPerLap
local turns = (laps * kInstrPerLap) / kBudget
local turn_ms = (kBudget * ns_instr) / 1000000.0

-- **The verdict, said with the numbers of this board**: a turn is the time a finger on the glass and a key of the board
-- wait before the kernel looks at them again, so the number it is compared with is **the hand** and not the glass. Ten
-- milliseconds is the threshold a person does not notice; a page of ink is about 120 ms with `du` (docs/95, E-7), and it
-- is said too because it is the other wait of this board.
local ink_ms = 120
local hand_ms = 10

local function say(text)
  console.print('luaperf: ' .. text)
end

say(string.format('%s on %s%s', sys.name, sys.board, sys.emulator and ' (the emulator: the price is its model)' or ''))
say(string.format('two lengths, %d and %d laps: %.0f and %.0f ns a lap', probe_laps, laps, probe_ns_lap, ns_lap))
say(string.format('%d laps in %d ms: %.1f turns, %.1f ns an instruction', laps, wall_ms, turns, ns_instr))
say(string.format("a turn of %d instructions is worth %.1f ms, the kernel's own turn in it", kBudget, turn_ms))
say(string.format('a key waits at most %.1f ms, against the %.0f ms a page of ink costs', turn_ms, ink_ms))
if turn_ms > hand_ms then
  say(string.format('over %d ms a hand waits: cut the slice with `cfg turn`', hand_ms))
else
  say(string.format('under %d ms a hand does not notice it: the slice is not what makes a key slow', hand_ms))
end

-- **And the same numbers on the glass**, because the console of a board is a cable and a person holding the board is not
-- carrying one. A board with no glass keeps the console and nothing else (docs/30).
if not caps.display then
  say('this board has no glass: the numbers stay in the console')
  return
end

local screen = ui.caps()

-- **The page is the app's and the app owns all of it**: a control paints inside its own box, so the air around the panel
-- would keep whatever the glass had before it unless the app washes the page first (docs/30, docs/50). **And the quality
-- is the system's** (0.179.5): this is a page of words, and the pass that flashes is for a photograph (docs/50).
ui.clear(15)

local page = ui.panel(screen.width, screen.height, 'vertical')
if page == nil then
  say('there is no room for the page on this glass')
  return
end
page.padding = 20
page.gap = 12
page:place(0, 0)

local function line(text, scale, ink)
  local label = ui.label(screen.width - 40, ui.line_height(scale) + 8)
  if label == nil then return end
  label.text = text
  label.scale = scale
  label.color = ink
  label.background = 15
  page:add(label)
end

line(string.format('%.1f ns an instruction', ns_instr), 4, 0)
line(string.format('a turn: %.1f ms', turn_ms), 3, 0)
line(string.format('a key waits %.1f ms', turn_ms), 3, 0)
line(string.format('a page of ink: %.0f ms', ink_ms), 2, 8)
line(turn_ms > hand_ms and 'the turn is felt' or 'the turn is not felt', 3, 0)
ui.present()

-- The page stays until the system takes it: three taps of the middle key, the same way out as any other app.
while true do sys.delay(50):await() end
