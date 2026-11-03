-- dialog: a card over the page with words and two doors.
--   run dialog
--
-- **A dialog is a control and the card is drawn by the system**: the app says the title, the message, the note and
-- the two labels, and the kernel decides where a line breaks, where the doors are and how the emphasised one is
-- drawn. Not a pixel of it is painted here (docs/30, docs/90).
--
-- **Two signals and not one**: `emphasis` is the door of default and it is inverted —ink with the words in
-- paper— and `choice` is the door the dial is on, which wears the ring. On this glass there is no colour for "this
-- is the good one", so a card that used one signal for both would lie about which is which.
--
-- **The dial walks the two doors** and the centre fires `on_accept` or `on_cancel`; a finger picks the door it is
-- on. Here every answer puts the card back where it was, because closing it is the app's business and a page with
-- one card has nothing else to show.
local caps = sys.caps()
if not caps.display then
  console.print('dialog: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Dialog')

-- The door of the page that opens the card: a plain button of the system, so the card is not the only thing here.
local open = ui.button(240, 56)
open:place((view.width - 240) / 2, 60)
local open_label = ui.label(220, 40)
open_label.text = 'Delete everything'
open_label.scale = 2
open_label.valign = 'middle'
open_label.align = 'center'
open_label:place(10, 8)
open:add(open_label)

local answer = ui.label(view.width - 40, 24)
answer.text = 'nothing asked yet'
answer.scale = 2
answer.color = 8
answer:place(20, 140)

local card = nil

local function close()
  if card ~= nil then
    card:release()
    card = nil
  end
end

local function ask()
  if card ~= nil then return end
  card = ui.dialog(380, 280)
  card:place((view.width - 380) / 2, 200)
  card.title = 'Delete?'
  card.text = 'This card deletes every note you wrote and there is no way back at all'
  card.note = 'Only yours'
  card.accept = 'Delete'
  card.cancel = 'Keep'
  card.emphasis = 1
  card.on_accept = function(self)
    -- **Read the action, close, then run it**: the card is gone before anything else happens (docs/90).
    answer.text = 'deleted'
    close()
    ui.present('gc16')
    console.print('dialog: deleted')
  end
  card.on_cancel = function(self)
    answer.text = 'kept'
    close()
    ui.present('gc16')
    console.print('dialog: kept')
  end
end

open.on_click = function(self)
  ask()
  ui.present('gc16')
end

ui.present('gc16')
console.print('dialog: the button opens the card; the dial walks its two doors')

-- The app stays while the page is on the glass: the card and the button answer as long as it lives.
while true do
  sys.delay(200):await()
end
