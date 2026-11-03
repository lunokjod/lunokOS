-- Keys: the keys of this board, on the console (docs/30, docs/50).
--   run keys
--
-- It asks the board what it has in its hands —how many keys, on which face, where
-- on that face and how they are built— and gives a function to a **position** and
-- not to a number. That is the point of the description: the middle key of a dial
-- and the middle of a single button are the same place, and an app should not care
-- which of the two it is standing on.
--
-- With one key (the ATOM) only the middle thing happens. With the three of the
-- Paper's dial, the two sides step a number and a key that stays down repeats:
-- the first repeat comes after half a second and then eight times a second, which
-- is the rhythm of a keyboard and not of a finger.
--
-- The text goes to the console because no board has a screen driver yet (docs/90).

local keys = button.list()
if #keys == 0 then
  console.print('this board has no keys')
  return
end

-- What a position does: up and right add, down and left take away, and the middle
-- of a face says where the number is. Nothing here knows how many keys there are
-- or which pin each one is on.
local function direction(spot)
  if spot == 'up' or spot == 'right' then return 1 end
  if spot == 'down' or spot == 'left' then return -1 end
  return 0
end

-- The repeat lives in the app and not in the system, on purpose: what a held key
-- does depends on what the key does —a keyboard repeats letters, a volume key
-- repeats steps, a menu key repeats nothing— and that is the app's business. The
-- system only says which key is down and for how long (docs/30).
local REPEAT_AFTER_MS = 500
local REPEAT_EVERY_MS = 125

local value = 0
local was_down = {}   -- key -> was it down in the frame before
local next_at = {}    -- key -> the held time at which the next step is due
local step_of = {}    -- key -> +1, -1 or 0

console.print('keys: ' .. #keys)
local compound = #keys > 1
local group = keys[1].group
for i = 1, #keys do
  local key = keys[i]
  console.print('  ' .. key.key .. ': ' .. key.face .. '/' .. key.spot .. ' ' ..
                  key.kind .. ' group ' .. key.group)
  was_down[key.key] = false
  next_at[key.key] = REPEAT_AFTER_MS
  step_of[key.key] = direction(key.spot)
  if key.kind ~= 'compound' or key.group ~= group then compound = false end
end
if compound then
  console.print('the ' .. #keys .. ' keys are one ' .. keys[1].kind ..
                  ' control on the ' .. keys[1].face .. ' face')
end
console.print('value ' .. value)

while true do
  for i = 1, #keys do
    local key = keys[i].key
    if button.down(key) then
      -- Everything is measured in the time the key has been down, so there is no
      -- second clock to keep in step with: `held` comes from the board (docs/30).
      local held = button.held(key)
      local due = false
      if not was_down[key] then
        was_down[key] = true
        next_at[key] = REPEAT_AFTER_MS
        due = true
      elseif held >= next_at[key] then
        next_at[key] = held + REPEAT_EVERY_MS
        due = true
      end
      if due then
        if step_of[key] > 0 then
          value = value + 1
          console.print('up ' .. value)
        elseif step_of[key] < 0 then
          value = value - 1
          console.print('down ' .. value)
        else
          console.print('value ' .. value ..
                          ' (the middle of the face: three taps close the app)')
        end
      end
    else
      was_down[key] = false
    end
  end
  sys.delay(40):await()
end
