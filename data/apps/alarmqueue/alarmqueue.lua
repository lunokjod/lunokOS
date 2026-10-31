-- The system's alarm queue, by hand (docs/30, docs/60).
--   run alarmqueue
--
-- It says what this board allows (how many alarms at once, how close together, what
-- can wake it), arms three of them a few seconds apart, shows the queue in the order
-- the kernel keeps it, cancels the middle one and shows the queue again without it.
-- The two that are left are delivered to on_alarm, with how late they were.
--
-- The queue belongs to the system and not to this app: it is written to the key-value
-- store, so what is armed here is still armed after a restart.

function on_alarm(alarm_id, payload, meta)
  console.print('fired ' .. alarm_id .. ' (' .. tostring(payload) .. ') late ' ..
    meta.late_ms .. ' ms, missed ' .. meta.missed)
end

-- Inside a window (the app was not loaded when the alarm went off) the entry runs
-- first and then on_alarm: arming here would arm another alarm every time.
if not console.is_foreground() then return end

local caps = alarm.caps()
console.print('alarms: ' .. caps.max .. ' at once, closest ' .. caps.min_period_ms ..
  ' ms, wake ' .. caps.wake)

local function show(what)
  local list = alarm.list()
  console.print(what .. ': ' .. #list .. ' armed')
  local now = time.now()
  for _, a in ipairs(list) do
    local line = '  id ' .. a.id .. ', in ' .. (a.on - now) .. ' s'
    if a.raise then line = line .. ', raise' end
    if a.persist then line = line .. ', persist' end
    -- `repeat` is a reserved word in Lua, so the field of a table is read with brackets.
    local every = a['repeat']
    if every and every > 0 then line = line .. ', repeat ' .. every .. ' s' end
    console.print(line)
  end
end

local first = alarm.after(4000, 'first')
local middle = alarm.after(6000, 'middle')
local last = alarm.after(8000, 'last')
console.print('armed ' .. first .. ', ' .. middle .. ' and ' .. last)
show('queue')

-- The one in the middle is the one that goes: the queue has to close the gap, and
-- an app can only cancel its own.
console.print('cancelling ' .. middle .. ': ' .. tostring(alarm.cancel(middle)))
show('queue without the middle one')

sys.delay(9000):await()
console.print('done, ' .. #alarm.list() .. ' still armed')
