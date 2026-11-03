-- What the board knows about the hour, and a sleep it has to come back from
-- (docs/50, docs/60).
--   run rtccheck
--
-- First run: says where the hour comes from (a chip with a battery behind it, or the
-- chip's own counter), what can wake the board, where the battery is, and how long a
-- one second wait really took. Then it writes down the hour it is asking to come back
-- at and sleeps twenty seconds.
--
-- Second run (the system opens it again with `app.args.restore`): it reads the hour it
-- asked for, compares it with the one the board has, and says the difference in
-- milliseconds. That number is the drift of the wake, measured from an app, and it is
-- the same one `power last` keeps for the shell.

local note = fs.data('rtccheck.txt')

local function read_note()
  local file = fs.open(note, 'r')
  if not file then return nil end
  local text = file:read('*a')
  file:close()
  return tonumber(text)
end

local function write_note(ms)
  local file = fs.open(note, 'w')
  if not file then return end
  file:write(tostring(ms))
  file:close()
end

if not console.is_foreground() then return end  -- a window has nothing to do here

-- Back from the sleep: the only thing worth saying is how far off it was.
if app.args and app.args.restore then
  local asked = read_note()
  if not asked then
    console.print('back, but there is no note of the hour that was asked for')
    return
  end
  local now = time.now_ms()
  console.print('asked for ' .. asked .. ' ms, back at ' .. now .. ' ms')
  console.print('the wake was ' .. (now - asked) .. ' ms off (positive is late)')
  return
end

local caps = time.caps()
console.print('hour from: ' .. caps.source .. ' (wake ' .. caps.wake .. ')')
console.print('now: ' .. time.now() .. ' s, ' .. time.now_ms() .. ' ms since the epoch')

local handle = fs.open(note, 'w')
if handle then
  handle:write('0')
  handle:close()
end

local p = power.caps()
local modes = ''
if p.sleep_light then modes = modes .. 'light' end
if p.sleep_deep then modes = modes .. (modes ~= '' and ' deep' or 'deep') end
if modes == '' then modes = 'none' end
console.print('sleep: ' .. modes .. ', wake from: ' .. p.wake)

local b = power.battery()
if b then
  console.print('battery: ' .. b.mv .. ' mV, ' .. b.pct .. ' %')
else
  console.print('battery: this board cannot tell')
end

-- The clock while the system is awake, against a wait the app asked for.
local before = time.now_ms()
sys.delay(1000):await()
console.print('a wait of 1000 ms took ' .. (time.now_ms() - before) .. ' ms of the clock')

local secs = 20
local asked = time.now_ms() + secs * 1000
write_note(asked)
console.print('asking to come back at ' .. asked .. ' ms, in ' .. secs .. ' s')

local ok, promise = pcall(power.sleep, { mode = 'deep', in_ms = secs * 1000 })
if not ok or not promise then
  console.print('this board cannot sleep like that (or nobody has set the hour)')
  return
end
promise:await()
console.print('this line only runs on a board that wakes without restarting')
