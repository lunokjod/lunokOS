-- Arm an alarm, leave, and come back later (docs/30, docs/60).
--   run later
--
-- First run: writes down the hour it is asking for, arms a **persistent** alarm five
-- seconds ahead that asks for the front, and **leaves** with `app.exit()`. Nothing of
-- this app stays in memory: the queue keeps the hour and the payload, which is why it
-- survives a restart and a board that switched itself off to wait for it.
--
-- When the alarm goes off the app is not loaded any more, so the system opens a window:
-- the entry runs (and returns at once, because a window is not the foreground) and then
-- `on_alarm` is called. There the app compares the hour it asked for with the one the
-- board has, and says the difference.
--
-- This is the shape of anything that has to happen later: an alarm, no coroutine, no
-- timer held in memory.

local note = fs.data('later.txt')
local secs = 5

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

-- In a window the entry is only used to load the app: the work is here.
function on_alarm(alarm_id, payload, meta)
  local asked = read_note()
  if not asked then
    console.print('back (' .. tostring(payload) .. '), but the note of the hour is gone')
    return
  end
  local now = time.now_ms()
  console.print('back (' .. tostring(payload) .. '), ' .. meta.late_ms .. ' ms late')
  console.print('asked for ' .. asked .. ', back at ' .. now .. ': ' .. (now - asked) .. ' ms off')
end

if not console.is_foreground() then return end  -- a window: nothing to arm here

local asked = time.now_ms() + secs * 1000
write_note(asked)
local id = alarm.at({ in_ms = secs * 1000, payload = 'later', raise = true, persist = true })
console.print('alarm ' .. id .. ' armed for ' .. asked .. ' ms, ' .. secs .. ' s from now')
console.print('leaving: the queue keeps it, and the board may power off while it counts')
app.exit()
