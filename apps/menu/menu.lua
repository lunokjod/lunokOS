-- menu: a contextual menu, which is a menu plus a place.
--   run menu
--
-- **The card is the system's and the place is the app's**: this app says the words of the options and where to put the
-- card —where the finger was when the finger asked— and not one pixel of it is painted here (docs/30, docs/90).
--
-- **A contextual menu is about the thing that was touched**: touching a note opens the card at that note with the
-- things that can be done to it, and the title of the card is the note's own name, so there is nothing to work out
-- about which note the options belong to.
--
-- **The options this app cannot do are hidden and not shown dead**: this build has nothing to share a note with, so
-- "Share" is `hidden` —it takes no room, the finger cannot press it and the dial never lands on it— and the option a
-- person is most likely to want is `primary`, which is the one that is drawn inverted (docs/90).
--
-- **The app reads the panel itself**: a card that is not on the glass yet is not a control the kernel can report a
-- press for, so the kernel cannot tell this app "a finger landed here". `touch.read()` gives the frame of the panel and
-- three things are decided with it: a finger on a note opens the card, a finger outside the card closes it, and a
-- finger on the card is the card's —the system presses the option and this app hears `on_pick`—.
--
-- **The card opens when the finger lifts**: a control that appears under a finger that is already down takes that same
-- contact on the next turn and letting go presses the option the dial is on, which is the first one. A menu with a
-- destructive option in it that fires because the finger that asked for the menu was still there is not a detail
-- (docs/30, docs/90).
--
-- It stops by itself after forty seconds, so a script can run it.
local caps = sys.caps()
if not caps.touch or not caps.display then
  console.print('this board needs a touch panel and a glass for this')
  return
end

local view = ui.caps()
local kCardWidth = 300
local kCardHeight = 240

ui.title('Menu')

local notes = {'Shopping', 'Ideas', 'Call the plumber'}
local line_h = 56
local first_y = 130
local marks = {}
local chosen = 0

ui.text(24, 40, 'menu: touch a note, and the menu opens where the finger is', 1, 8, 'gc16')
ui.text(24, 66, 'a finger outside the card closes it', 1, 8, 'gc16')

for i = 1, #notes do
  local row = ui.row(view.width - 40, line_h)
  row:place(20, first_y + (i - 1) * line_h)
  row.title = notes[i]
  row.detail = ''
  row.subtitle = ''
  row.marked = false
  marks[i] = row
end

local answer = ui.label(view.width - 48, 28)
answer.text = 'nothing chosen yet'
answer.scale = 2
answer.color = 8
answer:place(24, view.height - 60)

local card = nil
local card_x = 0
local card_y = 0

local function close_menu()
  if card ~= nil then
    card:release()
    card = nil
  end
end

-- **The card is moved into the view and not clipped by it**: a menu opened near the right or the bottom edge would
-- hang half of its options outside the glass, and an option nobody can press is not an option (docs/90).
local function place_card(x, y)
  card_x = x
  card_y = y
  if card_x + kCardWidth > view.width then card_x = view.width - kCardWidth end
  if card_y + kCardHeight > view.height then card_y = view.height - kCardHeight end
  if card_x < 0 then card_x = 0 end
  if card_y < 0 then card_y = 0 end
  card:place(card_x, card_y)
end

local function open_menu(x, y, line)
  if card ~= nil then return end
  card = ui.menu(kCardWidth, kCardHeight)
  card.title = notes[line]
  card.items = {
    {label = 'Open', tag = 'note'},
    {label = 'Rename'},
    {label = 'Delete', primary = true},
    {label = 'Share', hidden = true},
  }
  card.on_pick = function(self, index)
    -- **Read the action, close, then run it**: the card is gone before anything else happens (docs/90).
    local what = ({'opened', 'renamed', 'deleted', 'shared'})[index]
    answer.text = what .. ' ' .. notes[line]
    console.print('menu: ' .. answer.text)
    close_menu()
    ui.present('gc16')
  end
  place_card(x, y)
end

local function line_at(y)
  for i = 1, #notes do
    local top = first_y + (i - 1) * line_h
    if y >= top and y < top + line_h then return i end
  end
  return 0
end

if not ui.present('gc16') then
  console.print('the panel did not take the page')
  return
end
while not ui.idle() do
  sys.delay(10):await()
end
console.print('menu: touch a note; the options are Open, Rename and Delete')

-- **The frame of the panel and not a gesture**: what this app needs is where the finger is, and a gesture is a summary
-- that arrives when the finger has already left (docs/30). And **the card opens when the finger lifts and not when it
-- lands**: a control that appears under a finger that is already down takes that same contact, the kernel presses it
-- on the next turn and letting go picks the option the dial is on —which is the first one, and in a menu with "Delete"
-- in it that is a bug and not a detail (docs/30, docs/90)—.
local held = false
local press_x = 0
local press_y = 0
local press_line = 0
local press_on_card = false

-- **The page stays while there is somebody on it**: a menu that closes after forty seconds is a menu that vanishes
-- while the finger is on the way, and the shell takes every key after that. The way out is the shell's, and what the
-- person chose is said when the app is closed (docs/30, docs/90).
function on_close()
  console.print('menu: closed' .. ((chosen > 0) and (' (' .. notes[chosen] .. ')') or ''))
end

while true do
  local fingers = touch.read()
  if #fingers > 0 then
    if not held then
      held = true
      local f = fingers[1]
      press_x = f.x
      press_y = f.y
      press_on_card = card ~= nil and f.x >= card_x and f.x < card_x + kCardWidth and f.y >= card_y and
        f.y < card_y + kCardHeight
      press_line = 0
      if card ~= nil then
        -- A finger outside the card closes it at once; a finger on the card is the card's and the system takes it.
        if not press_on_card then
          close_menu()
          ui.present('gc16')
        end
      else
        press_line = line_at(press_y)
      end
    end
  elseif held then
    held = false
    if not press_on_card and card == nil and press_line > 0 then
      chosen = press_line
      for i = 1, #notes do marks[i].marked = (i == press_line) end
      open_menu(press_x, press_y, press_line)
      ui.present('gc16')
    end
    press_line = 0
    press_on_card = false
  end
  sys.delay(50):await()
end