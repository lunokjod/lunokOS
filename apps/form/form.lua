-- form: two fields that are chosen and not typed, and a band that says what happened.
--   run form
--
-- **A selector is a door, not a list**: the field says the question in the small dim face and the answer in the ink, and a
-- finger or the centre of the dial opens the list the app puts up —a `ui.menu`, drawn by the system like every other
-- control— and the choice is written in the field. The control holds no table it does not own (docs/30, docs/90).
--
-- **The empty value is a state of its own**: `empty` is what this app says "nothing chosen" in its own language, and it is
-- drawn where a value would be, so a field that has not been touched does not lie about it.
--
-- **The band of notifications stacks from the bottom and the newest is the one down**, nearest the finger: it is the one
-- that goes away first, and its cross is the column on the right of the card.
--
-- It stops by itself after forty seconds, so a script can run it.
local caps = sys.caps()
if not caps.display then
  console.print('form: this board has no glass')
  return
end

local view = ui.caps()
local hours = {'08', '09', '10', '11', '12', '13', '14', '15', '16', '17', '18', '19'}
local minutes = {'00', '05', '10', '15', '20', '25', '30', '35', '40', '45', '50', '55'}

ui.title('Form')

local note = ui.label(view.width - 48, 24)
note.text = 'an alarm, chosen and not typed'
note.scale = 2
note.color = 8
note:place(24, 40)

local card = nil
local chosen = 0

-- **The list is the app's and the card is the system's**: a `ui.menu` of the options, and its `on_pick` writes the index in
-- the field and turns `choosing` off, which is the only state the field has (docs/30).
local function open_list(field, items)
  if card ~= nil then return end
  chosen = field
  field.choosing = true
  card = ui.menu(240, 320)
  card:place(24, 200)
  card.title = field.label
  card.items = items
  card.on_pick = function(self, index)
    field.index = index
    field.choosing = false
    card:release()
    card = nil
    chosen = 0
    console.print('form: ' .. field.label .. ' ' .. field.items[index])
    ui.present('gc16')
  end
end

local hour = ui.selector(view.width - 48, 56)
hour:place(24, 90)
hour.label = 'Hour'
hour.items = hours
hour.on_open = function(self) open_list(self, hours) end

local minute = ui.selector(view.width - 48, 56)
minute:place(24, 160)
minute.label = 'Minute'
minute.items = minutes
minute.on_open = function(self) open_list(self, minutes) end

-- The band at the bottom of the page, which is where a notice wants to be.
local band = ui.notification_bar(view.width, 240)
band:place(0, view.height - 240)
band.on_close = function(self, item)
  table.remove(band.items, item)
  band.items = band.items
  console.print('form: notice ' .. item .. ' dismissed, ' .. #band.items .. ' left')
  ui.present('gc16')
end

ui.present('gc16')
console.print('form: the centre of the dial opens the list of a field')

-- **The page stays while there is somebody on it**: a form that closes after forty seconds closes before the person
-- has chosen the second field, and the shell takes every key after that. What was set is said when the app is closed
-- (docs/30, docs/90).
function on_close()
  console.print('form: closed with field ' .. tostring(hour.index) .. ' and field ' .. tostring(minute.index))
end

local announced = false
while true do
  -- When both fields have a value the band says so, once.
  if not announced and hour.index > 0 and minute.index > 0 then
    announced = true
    band.items = { {title = 'Alarm', text = hours[hour.index] .. ':' .. minutes[minute.index] .. ' set'} }
    ui.present('gc16')
  end
  sys.delay(100):await()
end
