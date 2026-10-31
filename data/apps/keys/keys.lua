-- Keys: the keys of this board, on the console (docs/30, docs/50).
--   run keys
--
-- It asks the board how many it has and does what fits: the ATOM gives one and the
-- Paper three. Key 0 is the **main** one —the only button of the ATOM, the middle
-- key of the Paper— and it is the system's: three taps of it close the app. The
-- side keys step a number —one down, one up— and a key that stays down steps
-- again: the first repeat comes after half a second and then eight times a
-- second, which is the rhythm of a keyboard and not of a finger.
--
-- The text goes to the console because a board with no screen driver yet has no
-- other place to put it (docs/90).

local count = button.count()
if count == 0 then
  console.print('this board has no keys')
  return
end

-- The repeat lives in the app and not in the system, on purpose: what a held key
-- does depends on what the key does —a keyboard repeats letters, a volume key
-- repeats steps, a menu key repeats nothing— and that is the app's business. The
-- system only says which key is down and for how long (docs/30).
local REPEAT_AFTER_MS = 500
local REPEAT_EVERY_MS = 125

local value = 0
local was_down = {}     -- key -> was it down in the frame before
local next_at = {}      -- key -> the held time at which the next step is due

-- What one press of a key does. The middle one does not step: it says where the
-- number is and leaves, because the system is counting its taps for the way out.
local function step(key)
  if key == 1 then
    value = value - 1
    console.print('down ' .. value)
  elseif key == 2 then
    value = value + 1
    console.print('up ' .. value)
  else
    console.print('value ' .. value .. ' (the main key: three taps close it)')
  end
end

for key = 0, count - 1 do
  was_down[key] = false
  next_at[key] = REPEAT_AFTER_MS
end

if count == 1 then
  console.print('keys: 1, the main one: three taps close the app and a press says the value')
else
  console.print('keys: ' .. count .. '; 0 is the main one, 1 and 2 the sides')
end
console.print('value ' .. value)

while true do
  for key = 0, count - 1 do
    if button.down(key) then
      -- Everything is measured in the time the key has been down, so there is no
      -- second clock to keep in step with: `held` comes from the board (docs/30).
      local held = button.held(key)
      if not was_down[key] then
        was_down[key] = true
        next_at[key] = REPEAT_AFTER_MS
        step(key)
      elseif held >= next_at[key] then
        next_at[key] = held + REPEAT_EVERY_MS
        step(key)
      end
    else
      was_down[key] = false
    end
  end
  sys.delay(40):await()
end
