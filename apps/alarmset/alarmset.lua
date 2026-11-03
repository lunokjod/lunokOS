-- Set an alarm by hand, from the console (docs/30, docs/60).
--   run alarmset
--
-- It asks for a number of seconds and arms a **persistent** alarm that asks for the
-- front. Then the app leaves, and from there the alarm belongs to the system: on a
-- board that keeps the hour in a chip and can stop being powered, the system puts
-- itself down and the quartz counts the wait (docs/50). When it fires, this app is
-- opened again, in front, and on_alarm runs.
--
-- Nothing here keeps a Lua function: the queue keeps the identifier, the payload and
-- the hour, which is what makes it survive a restart.

function on_alarm(alarm_id, payload, meta)
  console.print('ALARM ' .. tostring(payload) .. ' (' .. alarm_id .. ') late ' ..
    meta.late_ms .. ' ms, missed ' .. meta.missed)
end

if not console.is_foreground() then return end  -- a window: the alarm is all there is

console.write('in how many seconds? ')
local line = console.read_line():await()
local secs = tonumber(line)
if not secs or secs < 1 then
  console.print('that is not a number of seconds: ' .. tostring(line))
  return
end

-- `in_ms` and not `in`: `in` is a reserved word in Lua, and a table written with it does not even
-- parse (docs/30).
local id = alarm.at({ in_ms = secs * 1000, payload = 'wake up', raise = true, persist = true })
console.print('alarm ' .. id .. ' armed in ' .. secs .. ' s: it asks for the front and it is kept')
console.print('leave the board alone: the hour is the system\'s now, not this app\'s')
