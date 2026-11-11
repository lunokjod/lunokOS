-- **What went wrong is a notice of the system** (0.140.0, docs/46 C12): a book that will not open, a chapter with no
-- words this reader can read, a page that cannot be composed. The band says it on the glass and the card of the notice
-- opens the whole sentence when it is touched, because a sentence cut at the edge of a card is a sentence nobody read.
--
-- **A notice is the notice's own** (0.149.0, docs/46 D6): this module builds the band and the card and this module gives
-- them back, so the reader asks `up()` before it lets a finger or a key go by and never holds a control it did not make.
-- Nothing of it is saved: the band of an app lives in the app's memory and goes with the page (docs/30, docs/90).
local M = {}

-- **The size of the glass is the app's**, and the app owns the table and changes it when the system gives another view,
-- so this is a reference and not a copy (docs/46, D1).
local view = nil
local band = nil
-- **A band whose last card went is released in the turn after** (docs/90): a band with no card is a control that still
-- takes the dial, and a page that stops turning with the keys under a notice nobody can see is a bug of the notice.
local gone = false
local card = nil

-- **Is there anything of a notice on the glass?** The reader asks this before it lets the finger or the keys go to the
-- page: a notice is the last word on the glass (docs/30, docs/46 C12).
function M.up() return band ~= nil or card ~= nil end

-- **And is its turn over?** A band that lost its last card is given back in the turn after the one that heard the cross.
function M.gone() return gone end

function M.release()
  gone = false
  if card ~= nil then
    card:release()
    card = nil
  end
  if band ~= nil then
    band:release()
    band = nil
  end
end

-- The card of the whole sentence: one door, because a card that says what happened has nothing to cancel.
local function show_error(headline, text)
  if card ~= nil then card:release() end
  card = ui.dialog(math.min(view.w - 40, 500), math.min(320, view.h - 80))
  if card == nil then return end

  card.title = headline
  card.text = text
  card.accept = 'Close'
  -- **The one door is the good one**: an empty `cancel` is how a card is left with a single door across its width
  -- (docs/30, docs/90).
  card.cancel = ''
  card.emphasis = 2
  card:place(20, 120)
  card.on_accept = function(self)
    self:release()
    card = nil
    ui.present()
  end
  card.on_cancel = card.on_accept
  -- **The quality of the card and of the band is the system's** (0.179.5): both are boxes over a page that is already on the
  -- glass, and a `gc16` of a box is a flash of that box for a notice —two or three times per sentence, because the card
  -- goes, the band goes and the page under them comes back—. The kernel paints what changed with the pass that cleans and
  -- does not flash, and it is the app's to name only when it knows better than the ink (docs/30, docs/50).
  ui.present()
end

-- The band: the sentence cut at the edge, the cross that takes it away and the touch that opens the card.
function M.put(headline, text)
  M.release()
  local rows = math.min(240, view.h - 40)
  band = ui.notification_bar(view.w, rows)
  if band == nil then return end

  band.items = { { title = headline, text = text } }
  band.scale = 3
  band:place(0, view.h - rows)
  -- **The cross dismisses and the card opens the whole sentence** (docs/30, docs/90): the list is the app's and the
  -- control keeps none of it.
  band.on_close = function(self, item)
    table.remove(self.items, item)
    self.items = self.items
    if #self.items == 0 then gone = true end
    ui.present()
  end
  band.on_pick = function(self, item)
    local one = self.items[item]
    show_error(headline, (one ~= nil and one.text) or text)
  end
  ui.present()
end

-- **The glass, once and for all**: the app hands over its own table, and a view the system changes later is seen here
-- without another call because the table is the same one (0.149.0).
function M.set_view(one) view = one end

return M
