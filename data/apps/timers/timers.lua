-- Three timers at once, and the proof that waiting does not block (docs/30).
--   run timers
--
-- The app arms three timers and then waits on its own promise, printing a dot on
-- every turn. It is suspended while the kernel keeps turning, and the timers are
-- delivered between its turns, in the order they were armed: a timer is an alarm of
-- the system, not a sleep of the app (docs/60).

function on_alarm(alarm_id, payload, meta)
  console.print('timer ' .. tostring(payload) .. ' (' .. alarm_id .. ') late ' ..
    meta.late_ms .. ' ms')
end

if not console.is_foreground() then return end  -- a window: nothing to arm here

alarm.after(400, '400 ms')
alarm.after(800, '800 ms')
alarm.after(1200, '1200 ms')
console.print('three timers armed, ' .. #alarm.list() .. ' in the queue; waiting')

-- Eight waits of 200 ms: 1.6 s, more than the three timers need. Every dot is a turn
-- the kernel took while this app was suspended.
for _ = 1, 8 do
  sys.delay(200):await()
  console.write('.')
end
console.print('')
console.print('the app waited without stopping anything, ' .. #alarm.list() .. ' left')
