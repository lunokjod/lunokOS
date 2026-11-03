-- Sleepy: goes to sleep for a while and comes back where it was (docs/90).
--   run sleepy
--
-- A deep sleep is a board that restarts: the system writes down which app was in
-- front, closes it with its goodbye hooks and, when the RTC timer brings the chip
-- back, starts it again with `app.args.restore = true`. The app reads what it
-- saved instead of starting from nothing, which is what `on_save` is for.
--
-- The state lives in the app's own corner of the disk (`fs.data`), because the RAM
-- does not survive a deep sleep (docs/70, docs/90).

local cap_s = 300

local function read_elapsed()
  local file = fs.open(fs.data('sleepy.txt'), 'r')
  if not file then return 0 end
  local text = file:read('*a')
  file:close()
  return tonumber(text) or 0
end

-- on_save runs before the board goes down, with the app suspended, so it cannot
-- await: it only writes.
function on_save()
  local count = read_elapsed() + 1
  local file = fs.open(fs.data('sleepy.txt'), 'w')
  if not file then return end
  file:write(tostring(count))
  file:close()
  console.print('saved after sleep ' .. count)
end

if app.args and app.args.restore then
  console.print('back after the sleep, it kept: ' .. read_elapsed())
else
  console.print('first run, nothing saved yet')
end

console.print('going to sleep for ' .. cap_s .. ' s (' .. read_elapsed() .. ' sleeps so far)')
power.sleep({ mode = 'deep', in_ms = cap_s * 1000 }):await()
console.print('this line only runs on a board that wakes without restarting')
