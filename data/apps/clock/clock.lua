-- The clock and one alarm, so the board can be checked by hand:
--   run clock
--
-- In front it says the time and arms an alarm two seconds ahead. When the alarm
-- arrives the app is not loaded any more, so the system gives it a window: the
-- entry runs (in a window), and then on_alarm is called.
--
-- The entry runs in the window too, which is why arming the alarm there has to be
-- guarded: without the guard every window arms another alarm and the system calls
-- itself every two seconds, forever.

function on_alarm(alarm_id, payload, meta)
  console.print('alarm ' .. alarm_id .. ': ' .. tostring(payload) ..
    ', late ' .. meta.late_ms .. ' ms, missed ' .. meta.missed)
end

if not console.is_foreground() then
  return  -- a window: the alarm is the only thing to do here
end

local caps = time.caps()
console.print('now ' .. time.now() .. ' from ' .. caps.source ..
  ' (wake ' .. caps.wake .. ')')

local id = alarm.after(2000, 'ring')
console.print('alarm ' .. id .. ' armed, and ' .. #alarm.list() .. ' in the queue')

sys.delay(3000):await()
console.print('done, ' .. #alarm.list() .. ' still in the queue')
