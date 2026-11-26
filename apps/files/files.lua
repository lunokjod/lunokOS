-- files: the disk of this board, walked as one tree.
--
-- **IT IS THE FILES OF THE PAPER PROJECT (1.26.0), AND THE ONE THING THAT CHANGED IS THE ROOT** (the owner, 22/11:
-- *«en aquesta app hi han penstanyes per anar a l'SD o al littlefs, amb l'vfs ja no ens fa falta, to penja d'una sola
-- unitat»*). The old screen had a tab per memory —`sd` and `littlefs`— because its storage was a list of units; this
-- system has one tree (`docs/70`): `/littlefs`, `/sd`, `/data`, `/cfg`, `/apps`, `/res`, `/fonts`, `/session`, and the two
-- memories are two folders of it. So the strip of tabs and the `Disk` menu are gone, the screen opens at `/` —which is
-- what `ls /` says, mount by mount— and **everything else is the old app**, door by door.
--
-- EVERY DOOR IS IN A MENU BAR (the owner, 17/10, in the paper project: *«Pots afegir un menú contextual com a l'app Edit?
-- Amb copy paste cut, delete i amb View -> Show hidden files. Posa tots els botons al menú organitzat, també les pestanyes
-- SD i littleFS, tot ben net, menú i llista»*). `File` has going up, making something and changing a name; `Edit` the four
-- of a clipboard and the delete; `View` the hidden files and a refresh. The glyph buttons the first version had are gone:
-- a menu says what a door does, and a glyph has to be learned.
--
-- A ROW SAYS THE NAME AND, UNDER IT, WHEN AND HOW BIG (the owner, 4/10: «una vista de detall... nom, ctime, tamany»), with
-- the drawing of a folder or of a file in the column of the left. A folder keeps its slash, and a chosen row says so with a
-- star in front of its name: the list of this system frames **one** row (`marked`) and a selection of eight is not one
-- frame, so the mark that tells eight of them apart is a character of the text itself —which is also what a test can read—.
--
-- THE FINGER IS THREE THINGS (24/10): a tap opens a row, a swipe up goes up a level —the same door as `File → Up`— and a
-- hold starts a SELECTION so that `Edit → Copy` and `Edit → Delete` have more than one thing to work on. In a selection a
-- tap chooses a row instead of opening it, and the way out is a tap on the line of the folder and its room —all of it, not
-- only the words—. Out means out: no row stays chosen.
--
-- THE THREE CARDS —the name of a new thing, the new name of one, the question before deleting— are built when they are
-- asked and released when they are answered, which is the one place this port differs from the old one: there the pools of
-- controls were fixed and a card had to be built once and hidden, and here a control is given back with `release()` and the
-- page under it is painted again (docs/30, docs/90).
--
-- AND A PASTE IS A TASK WITH A MODAL AND THREE BARS (the owner, 17/10: *«l'app files ha de permetre copiar directoris
-- sencers i arxius molt grans, al fer tasques has de mostrar un modal amb una barra de progrés, el numero d'operacions per
-- fer a nivel lógic "arxiu X" i a nivel de blocks "5/345" copiats»*). A copy that ran to the end inside one call would keep
-- the loop inside the call and a folder of a few megabytes would freeze the glass with no bar and no sign of life, so it is
-- **two passes cut in turns**: the first walks the tree and counts the files and the blocks, the second walks it again and
-- copies a few blocks per turn. The old screen painted that modal by hand —and paid for it with the three passes of the
-- clean of a modal and a budget it had to ask for—; here it is a panel of the system with three `ui.progress` bars, and the
-- ink is the system's to mark (docs/30, docs/50).
--
-- WHO OPENS A FILE IS NOT THIS SCREEN'S BUSINESS (23/10): each app says in its manifest which extensions it opens and how it
-- wants to be asked, and this screen asks `apps.opener(name)` and starts what it is told with the path (`app.run`). A file
-- nobody claims is not opened at all and the line of state says why, with the extension in the sentence.
--
-- **The view is remembered for as long as the board is on** (the `state` session of docs/30): the folder and the page, and
-- nothing else. Which files were chosen is not the shape of a view —a selection is a thing a person is doing, and the row it
-- was on may not be there when the page comes back— and the hidden files are a decision about looking, which is the person's
-- and lives in `/cfg` and not in the block.

local caps = sys.caps()
if not caps.display then
  console.print('files: this board has no glass')
  return
end

-- ---------------------------------------------------------------- the numbers of the page

local kMargin = 12          -- the margin of the glass, the twelve of the house (docs/92)
local kBarHeight = 63       -- the row of the menu bar, reserved at the top of the content
local kRowHeight = 68       -- a row: a 48 px drawing and two lines of words
local kIcon = 48            -- the column of the drawing
local kIconX = 6
local kTextX = kIconX + kIcon + 10
local kNameScale = 3        -- 33 px of line with the face of the outline font of the disk
local kSmallScale = 2       -- 22 px: the line under the name and the path
local kDotsHeight = 37      -- the box of the paginator of the system (docs/92 §27)
local kSwipePx = 40         -- a finger that walks this far is asking for a page, and less is a finger that moved
local kCheapMode = 'du'     -- two tones: a row that changes is not a photograph
local kPageMode = 'gld16'   -- a page that arrives: sixteen tones and a pass that cleans
local kCardHeight = 340     -- the card of a question, in the middle of the page

-- **`run files keys` says where a row is**, which is what the suite of this system reads to put a finger on it: a test that
-- guessed a corner would be measuring the layout of the day it was written (the same door `tetris keys` opens, docs/85).
local asked = (app.args ~= nil) and (app.args.path or '') or ''
local want_keys = (asked:find('keys') ~= nil)
-- **I `run files /sd/notes` obre allà**: the prompt can point this page at a folder the way it points the editor at a file, and
-- the folder it names is where the screen starts —a path that is not a folder is not a place to open— (docs/30, docs/45).
local asked_path = (asked ~= '' and asked:find('keys') == nil) and asked or nil

-- What a `+` can create, in the order of the dropdown. Two, and the day there is a third it is one more line here.
local kinds = { 'Folder', 'File' }

-- **La icona d'un fitxer és el fitxer** (0.192.38, i és el que l'amo demanava: *«que l'app Files els utilitzi com icona per aquell
-- arxiu»*): a row of a picture draws the picture, scaled down, i el dibuix de la mena només hi és quan el sistema no en té, de cara
-- —una carpeta, un text, una foto que no s'ha pogut escalar—. **El cau el fa el sistema i no aquesta pàgina**: `fs.thumbnail` el
-- troba al costat del fitxer o l'escriu, i el que arriba aquí és un camí d'una foto que `ui.image` sap pintar (docs/30, docs/46).
local imageExt = { png = true, jpg = true, jpeg = true, gif = true, bmp = true, icn = true, bm = true,
                   -- **I un llibre, que té la seva portada al mateix lloc** (0.192.38): the cache of a cover is the one the
                   -- reader wrote with `epub.thumbnail` (docs/46, C11), and a book that has never been opened has none and the
                   -- row keeps the drawing of a file —the system refuses it in half a kilobyte, because a zip is not a picture.
                   epub = true }
local kFaceSide = 64        -- el costat més llarg del cau que es demana al sistema, que és el de la casa (docs/46, C11)

-- ---------------------------------------------------------------- the session `state`

-- **The block, and the pair that writes it and reads it** (docs/30, the code is the same in every app): what this page
-- remembers is the folder and the page, and neither is a decision of the person that has to survive a boot.
local kStateBytes = 1024
local kLayout = 'v1'        -- bumped when the shape of the block changes, and never migrated

local state_block = nil
local function the_state()
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  -- **A Lua pattern does not escape with a backslash** —`%` is its escape— so one `\\` in the literal is one backslash.
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'page', 'path' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  -- **El text s'acaba a la primera marca de zero** (0.189.52): el bloc fa mil vint-i-quatre bytes i el que s'hi escriu és
  -- més curt, o sigui que el que hi ha darrere és el zero amb què es va fer.
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end
  local now = { check = {}, text = {} }
  local first = true
  for line in body:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kLayout then return nil end
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

-- ---------------------------------------------------------------- what the page is now

-- **La vista és de `ui.caps()` i no de `sys.caps()`**: the first says what the glass gives this page —`width`, `height` and
-- the row where it starts— and the second says what the board has at all (`display`, `touch`, the keys). A page measured
-- from the wrong one is a page of zero pixels (docs/30, docs/40).
local first_view = ui.caps()
local wid, hei = first_view.width, first_view.height

local path = '/'               -- where the screen is
-- **El camí d'un fill d'una carpeta**, que és el que demanen la fila d'una foto i la còpia d'un arbre.
local function join_path(dir, name)
  if dir == '/' then return '/' .. name end
  return dir .. '/' .. name
end

local page = 1                 -- the page of the folder on the glass (one based, as a person says it)
local per_page = 8             -- the rows that fit a page, measured from the box of the list
local pages = 1                -- how many pages the folder has
local total = 0                -- how many entries it has (the ones this screen shows)

local entries = {}             -- the entries of the page, in the order the disk gave them
local chosen = {}              -- the row of the page (1 based) -> true, when the screen is selecting
local multi = false            -- the screen is selecting rows, one or many
local marked = 0               -- the row of the page the dial is on (1 based, 0 = none)
local status = ''              -- the line at the foot: what just happened
local showHidden = false       -- the hidden names, a decision of the person and not of the block

local clip = nil               -- { cut = bool, items = { {path, name, dir, size} } } what was chosen to paste
local card = ''                -- '' | 'new' | 'rename' | 'delete': what is being asked
local victim = nil             -- the entry the rename card is about
local kind = 1                 -- 1 = folder, 2 = file (the order of `kinds`)

-- The copy in flight: the modal and its numbers (see the task at the end of the file).
local task = nil
local queue = {}               -- what a paste has still to do, one item at a time
local queue_done = 0           -- how many of those are already done
local queue_last = ''          -- the name of the last one that finished

-- The controls of the page, which are built once and given back when the size changes.
local root = nil               -- the overlay of the page
local content = nil            -- everything that is not the bar
local where_line = nil         -- the panel of the path and the room: the way out of a selection
local where_label = nil
local room_label = nil
local the_list = nil
local the_dots = nil
local the_bar = nil
local card_panel = nil
local card_sheet = nil
local card_title = nil
local card_kind = nil
local card_box = nil
local card_no = nil
local card_yes = nil

-- The rows, which are built again on every page: `row_controls` is what has to be given back.
local row_controls = {}

-- ---------------------------------------------------------------- the words of the screen

-- A size, in something a person reads (a folder has no size at all).
local function size_text(n)
  if not n or n <= 0 then return '' end
  if n < 1000 then return n .. ' B' end
  if n < 1000000 then return math.floor(n / 1024) .. ' KB' end
  return math.floor(n / 1048576) .. ' MB'
end

-- When it was written, short, or the empty text when the disk cannot say. **A zero is not the 1st of January 2000**: it is
-- "I do not know", and a memory that keeps no dates answers zero, so nothing is written (docs/45).
local function date_text(sec)
  if not sec or sec == 0 then return '' end
  local d = time.date(sec)
  -- **The minute of this system is `min` and not `minute`** (docs/30): a field that is not there is a date that cannot be
  -- said, and a date that cannot be said is not printed (found on the emulator with the card of the suite, 22/11).
  if d == nil or d.day == nil or d.month == nil or d.hour == nil or d.min == nil then return '' end
  return string.format('%02d/%02d %02d:%02d', d.day, d.month, d.hour, d.min)
end

-- The date, and the size when there is one: two spaces of air between them.
local function sub_text(e)
  local when = date_text(e.mtime)
  local size = e.dir and '' or size_text(e.size)
  if when == '' then return size end
  if size == '' then return when end
  return when .. '  ' .. size
end

local function path_of(name)
  if path == '/' then return '/' .. name end
  return path .. '/' .. name
end

local function is_image_name(name)
  local ext = name:match('%.([%w_]+)$')
  return ext ~= nil and imageExt[ext:lower()] == true
end

-- **Every door of this system says what went wrong with a table** `{code, message}` and not with a sentence (docs/30), and
-- a page that prints it raw says `table: 0x3fc9` to a person. This is the one place that turns it into words.
local function why_text(err)
  if type(err) == 'table' then return tostring(err.message or err.code or 'it could not be done') end
  if err == nil then return 'it could not be done' end
  return tostring(err)
end

local function count_of(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

-- ---------------------------------------------------------------- the two preferences

-- **The hidden files are a decision about looking and not the shape of a view** (docs/30 § «El que no hi va»): it lives in
-- `/cfg/files/`, which is this app's settings on whichever unit answered, and the block of the session keeps the folder and
-- the page. A board that is switched on with them shown is a board where they were left shown (the old `prefs`).
local kSettingsFile = '/cfg/files/settings.txt'

local function read_settings()
  local text = fs.read(kSettingsFile)
  if text == nil then return end
  showHidden = (text:match('hidden=1') ~= nil)
end

local function save_settings()
  fs.write(kSettingsFile, 'hidden=' .. (showHidden and '1' or '0') .. '\n')
end

read_settings()

-- The four doors the rows and the cards call, declared here because a row is built while the file is still being read
-- (a Lua local is not a global: without this line the first tap on a row would call nothing).
local reload, apply_doors, open_entry, open_delete, refresh_rows
local build_page, measure_page
local page_panel

-- ---------------------------------------------------------------- the drawing of a row

-- **The two drawings of the old screen, in the primitives of this one.** The paper project took them from its icon font
-- (`f114` an outline folder, `f016` an empty file, at 32 px and drawn at 64); here they are drawn with lines, which costs no
-- file of the disk, is sharp at any size and —the reason it matters— **turns over with the row**: a glyph of a font was painted
-- by the list with the face of the row, and a PNG would be ink on ink the moment the row is the marked one.
--
-- **I el traç és de dos píxels, com el d'una font d'icones** (the owner, 22/11: *«Les icones no son les mateixes?»*): a
-- one-pixel outline on a panel of 540 x 960 is a hair, and the old figure was a stroke of the weight of the words. A frame of
-- one pixel drawn again one pixel inside is a frame of two.
local function a_frame(icon, x, y, w, h, ink)
  icon:frame(x, y, w, h, ink)
  icon:frame(x + 1, y + 1, w - 2, h - 2, ink)
end

-- The folder of the old: the tab on the left of the top edge and the body under it, both of two pixels.
local function draw_folder(icon, inverted)
  local ink = inverted and 15 or 0
  icon:clear(inverted and 0 or 15)
  a_frame(icon, 2, 8, 20, 10, ink)      -- the tab
  a_frame(icon, 2, 16, 44, 26, ink)     -- the body
end

-- The empty file of the old: a page with its top-right corner folded, drawn as the five sides and the fold.
local function draw_file(icon, inverted)
  local ink = inverted and 15 or 0
  icon:clear(inverted and 0 or 15)
  for shift = 0, 1 do
    local x = 11 + shift
    local y = 5 + shift
    local right = 37 - shift
    local bottom = 43 - shift
    local fold = 28 + shift
    icon:line(x, y, fold, y, ink)             -- the top, up to the fold
    icon:line(fold, y + 2, right - 2, y + 6, ink)  -- the fold itself
    icon:line(right, y + 6, right, bottom, ink)    -- the right side, from the fold down
    icon:line(x, y, x, bottom, ink)                -- the left side
    icon:line(x, bottom, right, bottom, ink)       -- the foot
    icon:line(fold, y, fold, y + 6, ink)           -- and the corner, so the fold reads as one
    icon:line(fold, y + 6, right, y + 6, ink)
  end
end

-- The line of state at the foot of the view, above the strip of pages: what happened in this folder —a folder made, a row
-- deleted, why a read failed— or that it is empty. It is not a control because a reason can be longer than the screen.
local status_label = nil

local function say_status(text)
  status = text or ''
  if status_label ~= nil then status_label.text = status end
  -- **The line of the foot is also a line of the log**: a page that says something only on the glass is a page a person
  -- with the board in their hand cannot read twice, and half of the questions about this screen are "what did it say"
  -- (docs/30, docs/95).
  if status ~= '' then console.print('[SAY] ' .. status) end
end

-- ---------------------------------------------------------------- a folder

-- **A selection is of this page of this folder**: the rows are about to be others, so a rectangle of them that stayed
-- chosen would be choosing files nobody touched (24/10). Every navigation calls this and nothing else clears it.
local function clear_selection()
  chosen = {}
  multi = false
  marked = 0
  if the_list ~= nil then the_list.marked = 0 end
end

local function chosen_entries()
  local out = {}
  for i = 1, #entries do
    if chosen[i] then out[#out + 1] = { row = i, e = entries[i] } end
  end
  return out
end

-- **La cara d'una fila** (0.192.38): the path of the picture the system keeps beside the file and the box it is drawn in, or
-- nothing at all —a folder, a text, a picture the system could not scale— and then the row draws the shape of its kind. **La cara
-- no s'engrandeix**: a picture of sixteen pixels is drawn at sixteen, perquè una foto ampliada és una foto pitjor (docs/30, docs/45).
local function the_face_of(e)
  if e.dir or e.name == nil then return nil end
  local ext = e.name:match('%.([^.]+)$')
  if ext == nil or not imageExt[ext:lower()] then return nil end
  local face = fs.thumbnail(join_path(path, e.name), kFaceSide)
  if face == nil then return nil end
  local info = ui.image_info(face)
  if info == nil or (info.width or 0) < 1 or (info.height or 0) < 1 then return nil end
  local side = math.max(info.width, info.height)
  local scale = kIcon / side
  if scale > 1 then scale = 1 end
  local w = math.max(1, math.floor(info.width * scale + 0.5))
  local h = math.max(1, math.floor(info.height * scale + 0.5))
  return face, w, h
end

-- One row of the page: the drawing, the name and the line under it. **The star in front of a chosen name is the mark of the
-- selection** —the list of the system frames one row and a selection is not one— and it is also what a test reads.
local function add_row(the_row, width, index)
  local e = entries[index]
  if e == nil then return end

  local entry = ui.list_entry(width, kRowHeight)
  if entry == nil then return end
  entry.background = 15

  -- **A ROW HAS TO BE A STOP OF THE DIAL OR THE FINGER NEVER REACHES IT** (found on the board, 22/11: *«no deixa navegar per
  -- subdirectoris»*). `focusable = false` was this app's way of keeping the dial out of the list —the paper project did the
  -- same— and in this system a control that is not a stop is not in the walk of the finger either: the tap of a row landed
  -- nowhere and the page was a picture. The stop is also the door of the middle key, which is the dial's own, and that is what
  -- makes the row answer both.
  --
  -- **I una fila d'una altra pàgina no actua**: `mine` is the table of the page this row was built for, and a tap that arrives
  -- after the page changed (a second press of the middle key on a board where the dial presses the control and the app hears
  -- the key as well) finds another table and does nothing (docs/30, docs/90).
  local mine = entries

  -- The ring of the dial is on a row and the mark of the screen is that row: the block does not keep it —a row number is not a
  -- shape— and the centre of the panel opens it (see the keys at the end of the file).
  entry.on_focus = function(self, on)
    if not on then return end
    marked = index
    the_list.marked = index
  end

  local icon = ui.canvas(kIcon, kIcon)
  if icon ~= nil then
    icon:place(kIconX, (kRowHeight - kIcon) // 2)
    -- **El camí de la cara es pregunta una vegada per fila i no a cada pintada**: `fs.thumbnail` pot arribar a escalar la foto la
    -- primera vegada, i això no es fa dins d'un `on_draw` —que és el moment de pintar i no el de treballar— (docs/30, docs/50).
    local face, face_w, face_h = the_face_of(e)
    icon.on_draw = function(self, inverted)
      if face ~= nil then
        self:clear(inverted and 0 or 15)
        local drawn = self:image((kIcon - face_w) // 2, (kIcon - face_h) // 2, face, face_w, face_h, kPageMode)
        if drawn then return end
      end
      if e.dir then draw_folder(self, inverted) else draw_file(self, inverted) end
    end
    entry:add(icon)
  end

  local name = e.dir and (e.name .. '/') or e.name
  if chosen[index] then name = '* ' .. name end

  -- **Les caixes de les dues línies s'aturen ABANS de la línia del separador** (0.190.257, trobat amb el vidre a la mà): a
  -- label paints its background before its words, and a box that reaches the bottom of the row erases the piece of the
  -- separator it covers —the line came out cut at the width of the subtitle and the rest of it read as a stray dash at the
  -- right—. The four pixels of air of a box are not needed: the line of a face is exactly its height.
  local title = ui.label(width - kTextX - kMargin, kNameScale * ui.line_height(1))
  if title ~= nil then
    title.text = name
    title.scale = kNameScale
    title.color = 0
    title.background = 15
    title:place(kTextX, 5)
    entry:add(title)
  end

  -- **A row that has nothing to say under its name has no line there**: the size of a folder and the date of a memory that
  -- keeps no dates are both nothing, and a control built for an empty text is a control the page pays for and paints (which is
  -- also the only way this glass shows a 4 px mark at the end of an empty label's box, found on the emulator, 22/11).
  local sub = sub_text(e)
  local under = nil
  if sub ~= '' then under = ui.label(width - kTextX - kMargin, kSmallScale * ui.line_height(1)) end
  if under ~= nil then
    under.text = sub
    under.scale = kSmallScale
    under.color = 0
    under.background = 15
    under:place(kTextX, 5 + kNameScale * ui.line_height(1))
    entry:add(under)
  end

  -- **A tap opens and a hold selects** (the finger of this screen): in a selection a tap chooses instead of opening, which
  -- is what the whole mode is for.
  entry.on_click = function()
    if task ~= nil then return end
    if entries ~= mine then return end
    if multi then
      chosen[index] = (not chosen[index]) or nil
      marked = index
      the_list.marked = index
      refresh_rows()
      return
    end
    open_entry(index)
  end

  -- **The first hold starts the selection and the second one asks to delete** (24/10 and 17/10): what is about to be lost
  -- is said with its name, and what answers is a button of the card.
  entry.on_long = function()
    if task ~= nil or card ~= '' then return end
    if entries ~= mine then return end
    if multi and chosen[index] then
      open_delete()
      return
    end
    console.print('[SEL] on')
    multi = true
    chosen[index] = true
    marked = index
    the_list.marked = index
    refresh_rows()
  end

  the_list:add(entry)
  row_controls[#row_controls + 1] = entry

end

-- **The rows are built again and the old ones are given back**: a page is at most ten controls of a row, and a control that
-- is not given back is a control the pools of the system cannot use for the next page (docs/30, docs/90).
local function build_rows()
  for i = 1, #row_controls do
    row_controls[i]:release()
  end
  row_controls = {}
  if the_list == nil then return end
  local box = the_list:caps()
  local width = (box ~= nil and box.width > 0) and box.width or (wid - 2 * kMargin)
  for i = 1, #entries do
    add_row(the_list, width, i)
  end
  -- **The list has to be told that its rows are other ones** (0.190.257): a control draws when something of it changed, and
  -- `add` puts a child in the tree without touching the father, so a list that is given rows after its first paint keeps the
  -- frame it drew and the placement of the entries of the page —which is the list's own drawing, not the app's— is never asked
  -- again. The rows of a page arrive at `0,0` and the last one covers the others: a screen of one row (found on the emulator
  -- with the eleven mounts of the tree, 22/11).
  the_list:invalidate()
end

local function update_where()
  if where_label ~= nil then where_label.text = path end
  local text = ''
  local space, why = fs.space(path)
  if space ~= nil and space.free ~= nil then
    text = size_text(space.free) .. ' free'
  elseif why ~= nil and why ~= '' then
    text = ''      -- a memory that cannot say how big it is says nothing (docs/70)
  end
  if room_label ~= nil then room_label.text = text end
end

-- **Reading the folder, and nothing else**: the entries of the page on the glass, the count of pages and the dots. Called by
-- every navigation —a folder entered, a page turned, a refresh— and it saves the view at the end, because it is the one point
-- where they all arrive.
function reload()
  clear_selection()
  entries = {}
  say_status('')

  local rows, why = fs.list(path)
  if rows == nil then
    entries, total, pages = {}, 0, 1
    say_status(why_text(why))
  else
    local shown = {}
    for i = 1, #rows do
      local e = rows[i]
      local hidden_name = (e.name ~= nil) and (e.name:sub(1, 1) == '.')
      if showHidden or not hidden_name then
        shown[#shown + 1] = { name = e.name, dir = (e.type == 'dir'), size = e.size or 0, mtime = e.mtime or 0 }
      end
    end
    total = #shown
    pages = math.max(1, math.ceil(total / math.max(1, per_page)))
    -- **A page that is not there any more is the last one**: the view can come from a folder that shrank —a card written
    -- from a computer, a file deleted from the prompt— and a page number is a position in a folder that has changed.
    if page < 1 then page = 1 end
    if page > pages then page = pages end
    local from = (page - 1) * per_page
    for i = 1, math.min(per_page, total - from) do
      entries[i] = shown[from + i]
    end
  end

  build_rows()

  -- **One line per navigation, and the geometry when the suite asks for it**: what the screen is showing and how many rows
  -- fit it, which is what turns "the app is dead" into a sentence on the cable (docs/30, docs/95).
  -- **I quantes files té el control**, que és el número que diu si una pàgina ha acumulat les que va deixar: la llista compta
  -- els seus fills per dir quantes pàgines té, i una fila que s'ha donat no pot comptar (0.190.257, i va ser el defecte que va
  -- fer que la pàgina no canviés en entrar en una carpeta).
  local live = (the_list ~= nil) and #the_list.children or 0
  console.print(string.format('[FILES] %s: %d entries, page %d/%d, %d per page, %d rows', path, total, page, pages,
                              per_page, live))
  if want_keys and the_list ~= nil then
    local box = the_list:caps()
    if box ~= nil then
      console.print(string.format('[FILES] keys row %d, list %d,%d %dx%d', kRowHeight, box.x or 0, box.y or 0,
                                  box.width, box.height))
    end
  end

  if the_dots ~= nil then
    the_dots.pages = pages
    the_dots.current = page
  end
  update_where()
  apply_doors()
  save_state({ page = page, path = path })
end

local function go_to(folder, which_page)
  path = folder
  page = which_page or 1
  reload()
  ui.present(kPageMode)
end

-- Up a level. It says whether it could, because "back" at the root means leaving the app, and that is the system's business
-- and not this view's.
local function go_up()
  if path == '/' then return false end
  local cut = path:match('^(.*)/[^/]+$')
  go_to((cut == nil or cut == '') and '/' or cut)
  return true
end

local function refresh()
  reload()
  ui.present(kPageMode)
end

-- ---------------------------------------------------------------- the menu bar and its doors

-- **The order of the doors is the order of the bar's table**, and `on_pick` answers two numbers —the title and the entry
-- inside it— so the labels are here, once, and not read back from the control: an entry that moves is a name that would
-- pick the wrong door (docs/30, docs/90).
local door_names = {
  { 'File', { 'Up', 'New...', 'Rename...' } },
  { 'Edit', { 'Copy', 'Cut', 'Paste', '-', 'Delete' } },
  { 'View', { 'Show hidden files', 'Refresh' } },
}

local function door_label(title, entry)
  local group = door_names[title]
  if group == nil then return nil end
  return group[2][entry]
end

-- **A door that cannot be done now is dead and not gone**: hiding one would move the rest of the menu under the finger that
-- has just pressed it, and a door that moves is a door that has to be looked for again (docs/30, docs/45).
apply_doors = function()
  if the_bar == nil then return end
  local busy = (task ~= nil) or (card ~= '')
  local how_many = count_of(chosen)
  -- **L'arrel no és un disc i no s'hi fa res nou** (0.192.40, l'amo: *«a l'arrel no ofereixin “New...” no es un disc real»*): `/` is
  -- where the units are mounted and not a folder of one, so `fs.mkdir('/x')` answers *«that path is not one this app may use»* —
  -- the door was offered and the disk said no, which is a page that lies about what can be done— (docs/30, docs/70).
  local on_a_disk = (path ~= '/')
  the_bar.items = {
    { title = 'File', items = {
        { label = 'Up', enabled = (not busy) and (path ~= '/') },
        { label = 'New...', enabled = (not busy) and on_a_disk },
        { label = 'Rename...', enabled = (not busy) and (how_many == 1) },
    } },
    { title = 'Edit', items = {
        { label = 'Copy', enabled = (not busy) and (how_many > 0) },
        { label = 'Cut', enabled = (not busy) and (how_many > 0) },
        { label = 'Paste', enabled = (not busy) and (clip ~= nil) },
        '-',
        { label = 'Delete', enabled = (not busy) and (how_many > 0) },
    } },
    { title = 'View', items = {
        { label = 'Show hidden files', checked = showHidden, enabled = not busy },
        { label = 'Refresh', enabled = not busy },
    } },
  }
end

-- ---------------------------------------------------------------- the clipboard

-- A NAME THAT IS FREE, WHICH IS WHAT THE PASTED FILE IS CALLED. The usual way of a browser: what arrives keeps its name when
-- the name is free, and when it is not it is `name 2`, `name 3`... The number goes **before** the extension and not after it,
-- because `photo 2.jpg` is read as a photo and `photo.jpg 2` is not read as anything (the old screen's rule).
local function name_is_there(name)
  for i = 1, #entries do
    if entries[i].name == name then return true end
  end
  return false
end

local function free_name(base)
  if not name_is_there(base) then return base end
  local stem, ext = base:match('^(.*)%.([^%.]+)$')
  if not stem then stem, ext = base, nil end
  for n = 2, 20 do
    local name = stem .. ' ' .. n .. (ext and ('.' .. ext) or '')
    if not name_is_there(name) then return name end
  end
  return base
end

-- Copy means "remember these", and what is remembered is the whole path of each: the paste can happen in another folder and
-- in another memory, which is the whole point of a tree with two disks in it. A FOLDER IS REMEMBERED TOO: it goes with
-- everything inside, and the walk is the task's (below). And it is a list and not one item (24/10).
local function start_copy(cut)
  local picked = chosen_entries()
  if #picked == 0 then return end
  clip = { cut = cut == true, items = {} }
  for i = 1, #picked do
    clip.items[#clip.items + 1] = { path = path_of(picked[i].e.name), name = picked[i].e.name,
                                    dir = picked[i].e.dir, size = picked[i].e.size }
  end
  if #picked == 1 then
    say_status((cut and 'cut: ' or 'copied: ') .. picked[1].e.name)
  else
    say_status((cut and 'cut ' or 'copied ') .. #picked .. ' items')
  end
  console.print(string.format('[COPY] chosen %d item(s)', #picked))
  clear_selection()
  apply_doors()
end

-- ================================================================ the copy in flight

--[[
 * A PASTE IS A TASK THAT IS CUT IN TURNS, AND THE MODAL SAYS THREE THINGS (the owner, 17/10): the whole copy in files, the
 * file in flight in blocks and the data moved in blocks. The system of this port has no `copy_begin`/`copy_step` —a copy is
 * two handles and a block at a time here— so what the old screen did with the kernel's job is done with `fs.open`: a few
 * blocks per turn of the loop, which is what keeps the board answering while a folder of a few megabytes goes by.
 *
 * TWO PASSES, because the bar needs a denominator: the first walks the tree and counts the files and the blocks, the second
 * walks it again and copies. Both are cut in turns, and the tree is walked with an explicit stack and not with a Lua
 * recursion: a turn must be able to stop anywhere —the modal is painted between two turns— and a stack that lives in a
 * local cannot be left half way.
 ]]

local BLOCK = 4096          -- the unit of the bar, which is the unit of the read
local STEP_BLOCKS = 4       -- blocks per turn of the loop: 16 KB
local WALK_STEPS = 32       -- entries examined per turn of the loop

local function push_dir(t, from, to)
  t.stack[#t.stack + 1] = { from = from, to = to, at = 0, items = nil }
end

-- The next entry of the tree, one at a time, from the deepest folder open.
local function next_entry(t)
  while #t.stack > 0 do
    local fr = t.stack[#t.stack]
    if fr.items == nil then
      local rows = fs.list(fr.from)
      if rows == nil or #rows == 0 then
        t.stack[#t.stack] = nil
      else
        fr.items = rows
        fr.at = 0
      end
    elseif fr.at >= #fr.items then
      t.stack[#t.stack] = nil
    else
      fr.at = fr.at + 1
      local e = fr.items[fr.at]
      local from = join_path(fr.from, e.name)
      local to = join_path(fr.to, e.name)
      if e.type == 'dir' then
        push_dir(t, from, to)
        return from, to, e.name, true, 0
      end
      return from, to, e.name, false, e.size or 0
    end
  end
  return nil
end

-- The first pass: how many files and how many blocks the tree has.
local function count_step(t)
  for _ = 1, WALK_STEPS do
    local _, _, name, dir, size = next_entry(t)
    if not name then return true end
    if not dir then
      t.files_total = t.files_total + 1
      t.blocks_total = t.blocks_total + math.ceil(size / BLOCK)
      t.current = name
    end
  end
  return false
end

-- **One file in flight**: two handles and a block at a time. The temporary is beside the target and the rename is what makes
-- it arrive whole, which is the promise of a write of this system (`fs.write` is atomic for the same reason, docs/70).
local function begin_copy(t, from, to, size)
  local src, why = fs.open(from, 'r')
  if src == nil then return false, tostring(why or ('cannot read ' .. from)) end
  local temp = to .. '.copy'
  local dst, no = fs.open(temp, 'w')
  if dst == nil then
    src:close()
    return false, tostring(no or ('cannot write ' .. to))
  end
  t.job = { src = src, dst = dst, to = to, temp = temp, blocks = 0 }
  t.local_blocks = 0
  t.local_total = math.ceil((size or 0) / BLOCK)
  t.blocks_total = math.max(t.blocks_total, t.blocks_done + t.local_total)
  return true
end

-- A job that does not end says why and leaves nothing half written.
local function drop_the_job(t)
  if t == nil or t.job == nil then return end
  if t.job.src ~= nil then t.job.src:close() end
  if t.job.dst ~= nil then t.job.dst:close() end
  if t.job.temp ~= nil then fs.remove(t.job.temp) end
  t.job = nil
end

-- The second pass: makes the folders, and copies one file at a time in blocks.
local function copy_step(t)
  if t.job then
    for _ = 1, STEP_BLOCKS do
      local chunk = t.job.src:read(BLOCK)
      if chunk == nil or #chunk == 0 then
        t.job.src:close()
        t.job.dst:close()
        local temp, to = t.job.temp, t.job.to
        t.job = nil
        t.local_blocks, t.local_total = 0, 0
        local ok, why = fs.rename(temp, to)
        if not ok then
          fs.remove(temp)     -- **the half file goes**: a copy that fails leaves nothing behind (docs/70)
          return true, why_text(why), false
        end
        t.files_done = t.files_done + 1
        return false, nil, false
      end
      local wrote = t.job.dst:write(chunk)
      if wrote == nil then return true, 'the copy stopped writing ' .. t.job.to, false end
      t.job.blocks = t.job.blocks + 1
      t.local_blocks = t.job.blocks
      t.blocks_done = t.blocks_done + 1
    end
    return false, nil, false
  end

  -- A single file has no tree to walk: its one job is the whole task.
  if t.file_root and not t.started then
    local ok, why = begin_copy(t, t.root_from, t.root_to, t.size)
    if not ok then return true, why, false end
    t.started = true
    return false, nil, false
  end

  for _ = 1, WALK_STEPS do
    local from, to, name, dir, size = next_entry(t)
    if not from then return false, nil, true end
    if dir then
      local ok, why = fs.mkdir(to)
      if not ok then return true, why_text(why), false end
    else
      local ok, why = begin_copy(t, from, to, size)
      if not ok then return true, why, false end
      t.current = name
      t.current_full = from
      console.print(string.format('[COPY] file %d/%d: %s', t.files_done + 1, t.files_total, name))
      return false, nil, false
    end
  end
  return false, nil, false
end

-- What the modal says, quantized: a repaint per block would be a screen that spends its life refreshing. Five steps of each
-- bar, one repaint per file (the old screen's arithmetic, and the reason its bars moved in five jumps).
local function task_percents(t)
  local total_pct, local_pct, blocks_pct = 0, 0, 0
  if t.phase == 'copy' then
    if t.files_total > 0 then
      local part = 0
      if t.local_total > 0 then part = t.local_blocks / t.local_total end
      total_pct = math.floor((t.files_done + part) * 100 / t.files_total)
    end
    if t.local_total > 0 then local_pct = math.floor(t.local_blocks * 100 / t.local_total) end
    if t.blocks_total > 0 then blocks_pct = math.floor(t.blocks_done * 100 / t.blocks_total) end
  end
  return total_pct, local_pct, blocks_pct
end

-- The path is cut FROM THE LEFT when it does not fit: the root is the part everybody knows and the name of the file is what
-- is being copied, so a path that does not fit keeps its end and not its beginning (the old screen's `fitLeft`).
local function fit_left(text, max_px)
  if ui.text_width(text, kSmallScale) <= max_px then return text end
  local body = text
  while #body > 4 do
    body = body:sub(2)
    local cut = '...' .. body
    if ui.text_width(cut, kSmallScale) <= max_px then return cut end
  end
  return '...'
end

-- ---------------------------------------------------------------- the modal of the copy

local modal = nil            -- the panel of the task, when it is up
local modal_title = nil
local modal_path = nil
local modal_rows = {}        -- three of { label, value, bar }

local function a_word(parent, width, text, scale, align)
  local label = ui.label(width, scale * ui.line_height(1) + 4)
  if label == nil then return nil end
  label.text = text
  label.scale = scale
  label.align = align or 'left'
  label.color = 0
  label.background = 15
  parent:add(label)
  return label
end

-- **The card of a task is a page of the system and not a drawing**: three rows of label, value and bar, which is what the
-- owner asked for in the paper project (*«No veig les tres barres que vull de progrés total/local/blocs»*), and the ink is
-- the system's to mark because a control that changes its text marks itself (docs/30, docs/90).
local function open_modal(t)
  local width = wid - 2 * kMargin
  local inner = width - 48

  modal = ui.panel(width, kCardHeight, 'vertical')
  if modal == nil then return end
  modal.padding = 24
  modal.gap = 10
  modal.background = 15

  modal_title = a_word(modal, inner, 'Copying', kNameScale, 'center')
  modal_path = a_word(modal, inner, '', kSmallScale, 'left')

  modal_rows = {}
  for i = 1, 3 do
    local line = ui.panel(inner, 30, 'horizontal')
    if line ~= nil then
      line.gap = 8
      modal:add(line)
      local left = a_word(line, math.floor(inner * 60 / 100), '', kSmallScale, 'left')
      local right = a_word(line, math.floor(inner * 40 / 100) - 8, '', kSmallScale, 'right')
      local bar = ui.progress(inner, 20)
      if bar ~= nil then
        bar.value = 0
        modal:add(bar)
      end
      modal_rows[i] = { label = left, value = right, bar = bar }
    end
  end

  local view = ui.caps()
  modal:place(kMargin, math.max(kMargin, (view.height - kCardHeight) // 2))
  ui.root():add(modal)
end

local function close_modal()
  if modal ~= nil then modal:release() end
  modal, modal_title, modal_path, modal_rows = nil, nil, nil, {}
end

-- What the card says right now. It is painted only when the quantized key changes, so the bars move in the steps the paper
-- can afford and not once per block.
local function paint_modal(t)
  if modal == nil then return end

  if t.phase == 'count' then
    modal_title.text = 'Counting'
  else
    modal_title.text = 'Copying'
  end
  modal_path.text = fit_left((t.phase == 'copy' and t.current_full) or t.root_from, (wid - 2 * kMargin) - 48)

  local total_pct, local_pct, blocks_pct = task_percents(t)
  local total_text, local_text, blocks_text

  if t.phase == 'count' then
    total_text = string.format('counting %d files', t.files_total)
    local_text, blocks_text = '-', '-'
  elseif t.files_total == 0 then
    total_text, local_text, blocks_text = 'no files inside', '-', '-'
  else
    local which = math.min(t.files_done + 1, t.files_total)
    total_text = string.format('%d/%d files - %d%%', which, t.files_total, total_pct)
    local_text = string.format('%d/%d blocks - %d%%', t.local_blocks, t.local_total, local_pct)
    blocks_text = string.format('%d/%d - %d%%', t.blocks_done, t.blocks_total, blocks_pct)
  end

  local words = { { 'Total', total_text, total_pct }, { 'This file', local_text, local_pct },
                  { 'Blocks', blocks_text, blocks_pct } }
  for i = 1, 3 do
    local row = modal_rows[i]
    if row ~= nil then
      if row.label ~= nil then row.label.text = words[i][1] end
      if row.value ~= nil then row.value.text = words[i][2] end
      if row.bar ~= nil then
        row.bar.value = (t.phase == 'count') and -1 or words[i][3]
      end
    end
  end
end

local function task_key(t)
  local total_pct, local_pct, blocks_pct = task_percents(t)
  local files = t.files_done
  local name = t.current or ''
  if t.phase == 'count' then
    files = math.floor(t.files_total / 16)
    name = ''
  end
  return string.format('%s|%d|%d|%d|%d|%s', t.phase, files, total_pct, local_pct, blocks_pct, name)
end

-- ---------------------------------------------------------------- the queue of a paste

local function finish_paste(said)
  close_modal()
  task = nil
  if said ~= nil then
    say_status(said)
  elseif queue_done > 1 then
    say_status('pasted ' .. queue_done .. ' items')
  else
    say_status('pasted: ' .. ((queue_last ~= '') and queue_last or '?'))
  end
  queue_done, queue_last = 0, ''
  reload()
  apply_doors()
  ui.present(kPageMode)
end

local function start_item(it)
  local name = free_name(it.name)
  local to = path_of(name)

  -- **A FOLDER CANNOT BE PASTED INSIDE ITSELF**: the walk would list the copy it is making and never end.
  if it.dir and to:sub(1, #it.path + 1) == it.path .. '/' then
    return false, 'the copy would put the folder inside itself'
  end

  task = {
    phase = it.dir and 'count' or 'copy',
    root_from = it.path, root_to = to, name = name,
    file_root = not it.dir, size = it.size or 0,
    stack = {},
    files_total = 0, files_done = 0,
    blocks_total = 0, blocks_done = 0,
    local_blocks = 0, local_total = 0,
    current = it.name, current_full = it.path,
    shown = '', ticks = 0,
  }

  if it.dir then
    -- **The root of the copy is made before the walk**: the walk enters it and only makes what it finds inside.
    local ok, why = fs.mkdir(to)
    if not ok then
      task = nil
      queue = {}
      return false, why_text(why)
    end
    push_dir(task, it.path, to)
  else
    task.files_total = 1
    task.blocks_total = math.ceil((it.size or 0) / BLOCK)
    task.local_total = task.blocks_total
  end

  console.print(string.format('[COPY] start: %s -> %s (%s)', it.path, to, it.dir and 'folder' or 'file'))
  open_modal(task)
  paint_modal(task)
  apply_doors()
  ui.present(kPageMode)
  return true
end

-- It answers whether the item started and, when it did not, why: the line at the foot is written by whoever asked and not
-- by the item, or a refusal would be wiped by the very thing that comes after it (the old screen's lesson).
local function next_paste()
  local it = queue[1]
  if it == nil then return false, nil end
  return start_item(it)
end

-- A paste is a queue of one item after another: when one ends the next one starts without giving the screen back, which is
-- what makes a selection look like one job (24/10).
local function task_step()
  local t = task
  if t == nil then return end

  if t.phase == 'count' then
    if count_step(t) then
      t.phase = 'copy'
      t.stack = {}
      t.files_done, t.blocks_done = 0, 0
      if not t.file_root then push_dir(t, t.root_from, t.root_to) end
      t.shown = ''
      console.print(string.format('[COPY] counted: %d files, %d blocks', t.files_total, t.blocks_total))
    end
    return
  end

  local failed, why, finished = copy_step(t)

  if failed then
    console.print('[COPY] failed: ' .. why_text(why))
    drop_the_job(t)
    task = nil
    queue = {}
    finish_paste('the copy failed: ' .. why_text(why))
    return
  end

  if finished then
    console.print(string.format('[COPY] done: %d files, %d blocks', t.files_done, t.blocks_done))
    task = nil
    queue_done = queue_done + 1
    queue_last = t.name
    if #queue > 0 then table.remove(queue, 1) end
    if #queue > 0 then
      local started, why = next_paste()
      if started then return end
      queue = {}
      finish_paste((why ~= nil) and ('the copy stopped: ' .. why) or nil)
      return
    end
    finish_paste(nil)
  end
end

-- A cut is a move inside one mount and a copy is a queue. **A move across mounts would be a copy and a delete**, which is
-- said instead of half done (the old screen's rule, and it is still true in a tree: the card and the flash are two disks).
local function mount_of(p)
  return p:match('^/([^/]+)') or ''
end

local function paste()
  if clip == nil or #clip.items == 0 then return end

  if clip.cut then
    if mount_of(clip.items[1].path) ~= mount_of(path) then
      say_status('a cut moves within one disk: copy and delete instead')
      ui.present(kCheapMode)
      return
    end
    local done, refused, why = 0, 0, ''
    for i = 1, #clip.items do
      local it = clip.items[i]
      local target = free_name(it.name)
      local ok, w = fs.rename(it.path, path_of(target))
      if ok then done = done + 1 else refused = refused + 1; why = why_text(w) end
    end
    local last = (#clip.items == 1) and clip.items[1].name or ''
    clip = nil
    queue_done, queue_last = done, last
    finish_paste(nil)
    if refused > 0 then
      say_status('moved ' .. done .. ', ' .. refused .. ' refused: ' .. why)
      ui.present(kCheapMode)
    end
    return
  end

  queue, queue_done, queue_last = {}, 0, ''
  for i = 1, #clip.items do
    local it = clip.items[i]
    queue[#queue + 1] = { path = it.path, name = it.name, dir = it.dir, size = it.size }
  end
  -- **The clipboard is not eaten by the paste**: a clipboard is a thing you paste twice, and the old screen paid for that
  -- lesson with a second `Paste` that did nothing at all (24/10).
  local started, why = next_paste()
  if not started then
    queue = {}
    reload()
    apply_doors()
    say_status((why ~= nil) and why or '')
    ui.present(kPageMode)
  end
end

-- ---------------------------------------------------------------- the cards

local keyboard = nil        -- the band of the system, asked once and hidden when a card goes
local kinds_menu = nil      -- the dropdown of the `New` card, which is a `ui.menu` of the system

local function the_keyboard()
  if keyboard == nil then
    local ok, kb = pcall(ui.keyboard)
    if ok then keyboard = kb end
  end
  return keyboard
end

local function drop_kinds_menu()
  if kinds_menu ~= nil then kinds_menu:release() end
  kinds_menu = nil
end

-- **La pàgina marxa mentre hi ha la pregunta** (0.192.40, l'amo: *«Fes-lo en una vista nova i menys problema»*): the page of the
-- list is given back and the question is the only thing in the view, which is what a view of its own means. It was a card over a
-- page and every frame the list asked for was a frame that painted the list over the question —the owner saw the folders of the
-- list on top of the card— and a finger on a button of the card could land on a row under it. Giving the page back is the same
-- door the app already has for the size of the view: one page, built when it is needed (docs/30, docs/90).
local function hide_the_page()
  if page_panel == nil then return end
  page_panel:release()
  page_panel, content, the_bar, the_list, the_dots = nil, nil, nil, nil, nil
  status_label, where_line, where_label, room_label = nil, nil, nil, nil
  row_controls = {}
end

-- **The page is built again when the answer is over**, with the size the glass has then: the band of the keyboard has changed
-- it while the question was up, and the list was never asked to fit the old one (0.192.40, docs/40, docs/90).
local function show_the_page()
  build_page()
  ui.present(kPageMode)
  measure_page()
  reload()
  ui.present(kPageMode)
end

-- **The card is given back when it is answered**, and so is the band: a card that stays is a page nobody can press, and a
-- band that stays is the bottom of the screen taken by a question that is over (docs/30, docs/90).
local function close_card()
  if card == '' and card_panel == nil then return end
  drop_kinds_menu()
  if card_panel ~= nil then card_panel:release() end
  if card_sheet ~= nil then card_sheet:release() end
  card_panel, card_sheet, card_title, card_kind, card_box, card_no, card_yes = nil, nil, nil, nil, nil, nil, nil
  card, victim = '', nil
  local kb = the_keyboard()
  if kb ~= nil then kb:hide() end
  show_the_page()
end

local function a_button(parent, width, text, on_click)
  local button = ui.button(width, 56)
  if button == nil then return nil end
  button.background = 15
  button.border = 0
  button.on_click = on_click
  local words = ui.label(width - 24, kNameScale * ui.line_height(1) + 4)
  if words ~= nil then
    words.text = text
    words.scale = kNameScale
    words.align = 'center'
    words.color = 0
    words.background = 15
    words:place(12, 4)
    button:add(words)
  end
  parent:add(button)
  return button
end

-- **La pregunta s'obre en una vista seva i no en una targeta sobre la llista** (0.192.40, l'amo: *«Fes-lo en una vista nova i
-- menys problema»*): a card over a page is a card that the page under it can paint over —the band of the keyboard gives the view
-- another size, the page is built again, and the question ends up under a page nobody draws again—, and its paper is a white
-- sheet the list shows through. **A page of its own covers the glass whole**, so nothing of the list is under the question and
-- the only thing that changes size is the question itself, which is four controls at the top (docs/30, docs/90).
local function new_card(title)
  local width = wid - 2 * kMargin
  hide_the_page()
  -- **El full de paper és un control i no el fons d'un panel**: a panel does not paint its `background` —it is the room that
  -- arranges what it holds—, and the sheet of the question was the list showing through the card. A label is opaque and fills
  -- its own box, which is the whole view here (docs/92, docs/30).
  card_sheet = ui.label(wid, hei)
  if card_sheet == nil then return false end
  card_sheet.background = 15
  card_sheet.color = 15
  card_sheet.text = ''
  card_sheet:place(0, 0)
  card_panel = ui.panel(wid, hei - kBarHeight - 16, 'vertical')
  if card_panel == nil then return false end
  card_panel.padding = kMargin
  card_panel.gap = 12
  card_panel:place(0, kBarHeight + 16)
  -- **La pregunta és una targeta per al sistema** (0.192.40): while it is up the page under it answers nothing, a finger that
  -- lands outside it is its cancel, and the cursor of a box under the question keeps quiet (docs/30, docs/90).
  card_panel.modal = true

  card_title = a_word(card_panel, width - 48, title, kNameScale, 'left')
  return true
end

local function kind_name()
  return kinds[kind] or kinds[1]
end

local function show_kind()
  if card_box ~= nil then card_box.text = 'New ' .. kind_name():lower() end
  if card_yes ~= nil then
    local words = card_yes.children[1]
    if words ~= nil then words.text = 'Create' end
  end
  ui.present(kCheapMode)
end

local function open_kinds()
  if kinds_menu ~= nil or card_panel == nil then return end
  kinds_menu = ui.menu(wid - 2 * kMargin, 170)
  if kinds_menu == nil then return end
  kinds_menu.items = kinds
  kinds_menu.index = kind
  kinds_menu.on_pick = function(self, which)
    kind = which
    self:release()
    kinds_menu = nil
    show_kind()
  end
  -- **I el dit de fora la tanca el sistema i aquesta pàgina ho ha de saber** (0.192.39): a context menu is the card of the system
  -- while it is up, and the finger that lands outside it folds it —the app hears `on_cancel` and forgets the object it was holding,
  -- or a second press on the kind would find a menu it can no longer open (docs/30, docs/90).
  kinds_menu.on_cancel = function() kinds_menu = nil end
  kinds_menu:place(kMargin, kBarHeight + 16 + 150)
  ui.root():add(kinds_menu)
  ui.present(kCheapMode)
end

-- A name is trimmed and it is not empty: a folder called " " is a folder nobody can see, and the system would refuse it with
-- a reason about a path, which is true and not what the person needs to hear.
local function clean_name(text)
  return (text or ''):gsub('^%s+', ''):gsub('%s+$', '')
end

-- Make the thing the card is asking about. What the disk says about the name —it is taken, it is not a name, there is no
-- room— is said ON THE CARD, which is where the person looking at the answer is.
local function create_thing()
  local name = clean_name(card_box ~= nil and card_box.text or '')
  if name == '' then
    if card_title ~= nil then card_title.text = 'A name is needed.' end
    ui.present(kCheapMode)
    return
  end
  local ok, why
  if kind == 1 then
    ok, why = fs.mkdir(path_of(name))
  else
    ok, why = fs.write(path_of(name), '')
  end
  if not ok then
    if card_title ~= nil then card_title.text = why_text(why) end
    ui.present(kCheapMode)
    return
  end
  close_card()
  reload()
  say_status('created: ' .. name)
  ui.present(kPageMode)
end

-- A name changes and the file does not move folder: `fs.rename` works inside one memory and does not write over anything —a
-- name that is taken comes back as a refusal and not as a lost file— and the reason goes on the card (docs/70).
local function rename_thing()
  local e = entries[victim or 0]
  if e == nil then return end
  local name = clean_name(card_box ~= nil and card_box.text or '')
  if name == '' then
    if card_title ~= nil then card_title.text = 'A name is needed.' end
    ui.present(kCheapMode)
    return
  end
  -- The same name is not a rename and is not an error either: it closes and says nothing, because nothing happened.
  if name == e.name then
    close_card()
    return
  end
  local before = e.name
  local ok, why = fs.rename(path_of(before), path_of(name))
  if not ok then
    if card_title ~= nil then card_title.text = why_text(why) end
    ui.present(kCheapMode)
    return
  end
  close_card()
  reload()
  say_status('renamed: ' .. before .. ' -> ' .. name)
  ui.present(kPageMode)
end

local function open_new()
  card = 'new'
  victim = nil
  if not new_card('New what?') then return end

  kind = 1
  card_kind = ui.selector(wid - 2 * kMargin - 48, 40)
  if card_kind ~= nil then
    card_kind.label = 'Kind'
    card_kind.items = kinds
    card_kind.index = 1
    card_kind.on_open = function() open_kinds() end
    card_panel:add(card_kind)
  end

  card_box = ui.textbox(wid - 2 * kMargin - 48, 56)
  if card_box ~= nil then
    card_box.text = 'New folder'
    card_panel:add(card_box)
  end

  local buttons = ui.panel(wid - 2 * kMargin - 48, 60, 'horizontal')
  if buttons ~= nil then
    buttons.gap = 20
    card_panel:add(buttons)
    card_no = a_button(buttons, (wid - 2 * kMargin - 68) // 2, 'Cancel', function() close_card() end)
    card_yes = a_button(buttons, (wid - 2 * kMargin - 68) // 2, 'Create', function() create_thing() end)
  end

  ui.root():add(card_panel)
  apply_doors()

  local kb = the_keyboard()
  if kb ~= nil and card_box ~= nil then
    kb.target = card_box
    kb:show('alpha')
  end
  ui.present(kPageMode)
end

-- Rename is the marked row and not the first one: the same rule as Delete, because a rename that guessed which name the
-- person meant is the kind of door that renames the wrong file. The box starts with the name it has, which is what somebody
-- wants to edit —the keyboard of this system has no "select all", so the caret goes at the end—.
local function open_rename()
  local picked = chosen_entries()
  if #picked ~= 1 then return end
  local e = picked[1].e

  card = 'rename'
  victim = picked[1].row
  if not new_card('Rename ' .. e.name .. ' to:') then return end

  card_box = ui.textbox(wid - 2 * kMargin - 48, 56)
  if card_box ~= nil then
    card_box.text = e.name
    card_box.cursor = #e.name
    card_panel:add(card_box)
  end

  local buttons = ui.panel(wid - 2 * kMargin - 48, 60, 'horizontal')
  if buttons ~= nil then
    buttons.gap = 20
    card_panel:add(buttons)
    card_no = a_button(buttons, (wid - 2 * kMargin - 68) // 2, 'Cancel', function() close_card() end)
    card_yes = a_button(buttons, (wid - 2 * kMargin - 68) // 2, 'Rename', function() rename_thing() end)
  end

  ui.root():add(card_panel)
  apply_doors()

  local kb = the_keyboard()
  if kb ~= nil and card_box ~= nil then
    kb.target = card_box
    kb:show('alpha')
  end
  ui.present(kPageMode)
end

-- Delete what is chosen, one by one, and a refusal does not stop the others: a folder that is not empty, or a file the disk
-- will not let go, is a line of the answer and not the end of the job (24/10).
local function remove_selected()
  local picked = chosen_entries()
  if #picked == 0 then return end
  local done, refused, why = 0, 0, ''
  for i = 1, #picked do
    local ok, w = fs.remove(path_of(picked[i].e.name))
    if ok then done = done + 1 else refused = refused + 1; why = why_text(w) end
  end
  clear_selection()
  reload()
  -- **After the reading and not before**: `reload()` clears the line when the folder reads well, so a message written before
  -- it is wiped by the very thing that comes next (the old screen's bug, found on its board).
  if refused == 0 then
    say_status('deleted: ' .. ((done == 1) and picked[1].e.name or (done .. ' items')))
  else
    say_status('deleted ' .. done .. ', ' .. refused .. ' refused: ' .. why)
  end
  apply_doors()
  ui.present(kPageMode)
end

-- **The question before deleting is the card of the system** (`ui.dialog`): it takes the dial and the finger while it is
-- alive, a tap outside it is its own cancel, and the sentence wraps in lines instead of ending in "..." (docs/30, docs/90).
open_delete = function()
  local picked = chosen_entries()
  if #picked == 0 then return end

  local title, text
  if #picked == 1 then
    local e = picked[1].e
    if e.dir then
      title = 'Delete the folder ' .. e.name .. '?'
      text = 'Only an empty folder can be deleted.'
    else
      title = 'Delete ' .. e.name .. '?'
      text = 'It cannot be undone.'
    end
  else
    title = 'Delete ' .. #picked .. ' items?'
    text = 'A folder only goes when it is empty, and this cannot be undone.'
  end

  local question = ui.dialog(wid - 2 * kMargin, 300)
  if question == nil then return end
  question.title = title
  question.text = text
  question.accept = 'Delete'
  question.cancel = 'Keep'
  question.emphasis = 1        -- the cancel is the door of default: what is about to be lost is not the finger's first answer
  question:place(kMargin, (hei - 300) // 2)
  question.on_accept = function(self)
    self:release()
    card = ''
    apply_doors()
    remove_selected()
  end
  question.on_cancel = function(self)
    self:release()
    card = ''
    apply_doors()
    ui.present(kCheapMode)
  end
  card = 'delete'
  ui.root():add(question)
  apply_doors()
  ui.present(kPageMode)
end

-- ---------------------------------------------------------------- opening a file

-- **What this screen decides is NOTHING**: each app says in its manifest which extensions it opens and with which switches,
-- and `apps.opener` answers the two things or the sentence that names the extension nobody claims. A file nobody claims is
-- not opened at all, and the line of the foot is where that is said (23/10).
open_entry = function(i)
  local e = entries[i]
  if e == nil then return end
  if e.dir then
    go_to(path_of(e.name))
    return
  end
  local target = path_of(e.name)
  local app_id, flags = apps.opener(target)
  if app_id == nil then
    say_status(why_text(flags))
    ui.present(kCheapMode)
    return
  end
  console.print('[OPEN] ' .. app_id .. ' ' .. target)
  local ok, why = app.run(app_id, true, (flags ~= '') and flags or nil, target)
  if not ok then
    say_status(why_text(why))
    ui.present(kCheapMode)
  end
end

-- ---------------------------------------------------------------- the page

local want_rows = false         -- a row changed and the rows have to be built again, once, in the loop

-- **The rows are built again when the loop says so and not inside the finger's hook**: the control the finger pressed is one
-- of the ones that would be given back, and giving back the control that is running its own callback is a door that the
-- system asks afterwards (docs/30, docs/90).
refresh_rows = function()
  want_rows = true
end

local function a_spacer(parent, width, height)
  local label = ui.label(width, height)
  if label ~= nil then parent:add(label) end
  return label
end

-- **The page is built once per size** and every navigation only fills the list again: the bar, the line of the path and the
-- strip of pages are the same controls all the time, which is what makes a page turn cost one repaint of the list.
build_page = function()
  if page_panel ~= nil then page_panel:release() end
  row_controls = {}
  the_bar, the_list, the_dots = nil, nil, nil
  status_label, where_line, where_label, room_label = nil, nil, nil, nil

  ui.clear(15)

  local inner = wid - 2 * kMargin

  page_panel = ui.panel(wid, hei, 'overlay')
  if page_panel == nil then return end
  page_panel:place(0, 0)

  content = ui.panel(wid, hei, 'vertical')
  if content == nil then return end
  content.padding = kMargin
  content.gap = 4

  -- **La fila de la barra és a sobre d'aquesta**: the bar paints its row and its underline over the top of the page, and the
  -- content starts under the line —a spacer shorter than the bar put the path of the folder inside the bar, cut by its line
  -- (found on the emulator, 22/11).
  a_spacer(content, inner, kBarHeight - kMargin)

  -- The path and the room of the memory, in the smallest letter of the interface: they are read once, in passing, and they
  -- do not have to shout —the rows are what the eye is here for— (the owner, 8/10: «el path i els MB free es massa gros»).
  where_line = ui.panel(inner, kSmallScale * ui.line_height(1) + 8, 'horizontal')
  if where_line ~= nil then
    content:add(where_line)
    local wide = math.floor(inner * 62 / 100)
    where_label = ui.label(wide, kSmallScale * ui.line_height(1) + 4)
    if where_label ~= nil then
      where_label.scale = kSmallScale
      where_label.text = path
      where_label.color = 0
      where_label.background = 15
      where_line:add(where_label)
    end
    room_label = nil
    if room_label ~= nil then
      room_label.scale = kSmallScale
      room_label.align = 'right'
      room_label.color = 0
      room_label.background = 15
      where_line:add(room_label)
    end
  end

  the_list = ui.list(inner, kRowHeight)
  if the_list ~= nil then
    the_list.weight = 1
    the_list.row_height = kRowHeight
    content:add(the_list)
  end

  -- The line of state, above the strip of pages: what happened in this folder, or that it is empty.
  status_label = ui.label(inner, kSmallScale * ui.line_height(1) + 4)
  if status_label ~= nil then
    status_label.scale = kSmallScale
    status_label.text = status
    status_label.color = 0
    status_label.background = 15
    content:add(status_label)
  end

  -- **THE STRIP OF PAGES IS THE LAST THING ON THE SCREEN** (the owner, 17/10: *«el paginador de boletes d' avall de tot»*):
  -- a status of the system and never a door, and the finger that turns a page is the swipe —a button that only repeats a
  -- gesture is a button that takes room from the ones that do something—.
  the_dots = ui.dots(inner, kDotsHeight)
  if the_dots ~= nil then
    the_dots.pages = pages
    the_dots.current = page
    content:add(the_dots)
  end

  page_panel:add(content)

  -- **The bar's frame is the whole page and it paints only its row and its menu**, so it goes after the content: a bar
  -- painted before the list would have its menu painted UNDER the document (docs/30, docs/90).
  the_bar = ui.menu_bar(wid, hei)
  if the_bar ~= nil then
    the_bar.bar_height = kBarHeight
    the_bar.on_pick = function(self, title, entry)
      local label = door_label(title, entry)
      if label == nil then return end
      console.print('[MENU] ' .. label)
      if label == 'Up' then
        go_up()
      elseif label == 'New...' then
        open_new()
      elseif label == 'Rename...' then
        open_rename()
      elseif label == 'Copy' then
        start_copy(false)
        ui.present(kCheapMode)
      elseif label == 'Cut' then
        start_copy(true)
        ui.present(kCheapMode)
      elseif label == 'Paste' then
        paste()
      elseif label == 'Delete' then
        open_delete()
      elseif label == 'Show hidden files' then
        showHidden = not showHidden
        save_settings()
        reload()
        apply_doors()
        ui.present(kPageMode)
      elseif label == 'Refresh' then
        refresh()
      end
    end
    page_panel:add(the_bar)
  end

  ui.root():add(page_panel)
  apply_doors()
end

-- **How many rows fit is asked to the box the list has**, and not guessed: the panel gives it the room that the bar, the
-- line of the path, the line of state and the strip leave, and a hand-measured number is a riddle that leaves paper under
-- the last row (the old screen learned it, and the list of this system asks for `weight` for the same reason).
measure_page = function()
  if the_list == nil then return end
  local box = the_list:caps()
  if box == nil or box.height <= 0 then return end
  -- **La banda del peu del control es reserva sempre** (found on the board, 22/11: *«Apareixen dos paginadors de boles.... un
  -- sobre el'altre!»*). The list of this system draws a paginator of its own when it has more than one page of rows, and the
  -- rows of this app are already one page of a folder: a `per_page` that filled the box to the last pixel made the control
  -- say two pages and the strip of the app said the same number under it. Reserving the band is what keeps the list at one
  -- page —its foot is not drawn— and the paginator of the screen is the app's, which is the one that knows how many pages the
  -- folder has (docs/30, docs/92).
  per_page = math.max(1, math.floor((box.height - kDotsHeight) / kRowHeight))
end

-- ---------------------------------------------------------------- the app

-- **The three keys are the app's on this board** (docs/30: the T5 E-Paper S3 Pro does not move the ring with them), and the
-- rows are not stops of the dial on purpose: the dial is the list's here and the finger is what walks it, so the side keys
-- turn the page and the centre opens the row with the mark —the one a tap or a hold left there— or the first one (the old
-- screen's own decision, and the one thing it took from the navigator).
local function turn_page(delta)
  local want = page + delta
  if want < 1 or want > pages then return false end
  page = want
  reload()
  apply_doors()
  ui.present(kPageMode)
  return true
end

local key_place = button.subscribe(function(event)
  if event.kind ~= 'press' then return end
  if task ~= nil or card ~= '' then return end
  local spot = event.spot
  if spot == 'left' or spot == 'up' then
    turn_page(-1)
  elseif spot == 'right' or spot == 'down' then
    turn_page(1)
  elseif spot == 'center' then
    if marked > 0 then open_entry(marked) elseif #entries > 0 then open_entry(1) end
  end
end)

-- **The view this page was left in**: the folder and the page, and a path that is not there any more falls back to the root —
-- the view of a folder that has been deleted is a screen of nothing with a strip of dots over it (docs/30).
do
  local was = load_state()
  if was ~= nil then
    local saved_path = was.path
    if saved_path ~= nil and saved_path ~= '' then
      local info = fs.stat(saved_path)
      if info ~= nil and info.type == 'dir' then
        path = saved_path
      end
    end
    local saved_page = tonumber(was.page)
    if saved_page ~= nil then page = math.max(1, math.floor(saved_page)) end
    console.print(string.format('files: came back to %s, page %d', path, page))
  end
end

-- **The prompt wins over the block**: a folder somebody named when starting the app is where they want to look, and the view
-- that comes back is for the start that named nothing.
if asked_path ~= nil then
  local info = fs.stat(asked_path)
  if info ~= nil and info.type == 'dir' then
    path = asked_path
    page = 1
  else
    console.print('files: ' .. asked_path .. ' is not a folder')
  end
end

build_page()
ui.present(kPageMode)     -- the page has to be on the glass before the list knows how tall its box is
measure_page()
reload()
ui.present(kPageMode)



console.print(string.format('files: %d entries in %s, %d per page', total, path, per_page))

-- ---------------------------------------------------------------- the loop

while true do
  -- **The room of the view is what the page is measured from**: the system can give the app another view —a turn, the band
  -- of the keyboard, the whole glass— and the page is built again from the new numbers (docs/40).
  local view = ui.caps()
  if view.width ~= wid or view.height ~= hei then
    wid, hei = view.width, view.height
    -- **Mentre hi ha la pregunta no hi ha pàgina a qui canviar la mida** (0.192.40): the question is a view of its own and the
    -- page under it is given back, so the numbers are taken and the page is built when the answer comes (docs/40, docs/90).
    if page_panel ~= nil then show_the_page() end
  end

  -- **The gestures of the system are taken and not left in the queue**: a drag is a swipe and anything else is read and
  -- dropped, because a queue nobody empties is a queue that grows (docs/30).
  local gesture = touch.gesture()
  while gesture do
    local kind = gesture.kind
    local dx, dy = gesture.dx or 0, gesture.dy or 0
    if task ~= nil then
      -- a copy takes the screen until it is done; a gesture of a page that is answering nothing goes nowhere
    elseif card ~= '' then
      -- a card takes the gesture away: it is a question, and the rows behind it are not what the finger is on
    elseif kind == 'drag' then
      if math.abs(dy) > math.abs(dx) then
        if dy <= -kSwipePx then
          if go_up() then console.print('[GESTURE] up') end
        end
      elseif dx <= -kSwipePx then
        turn_page(1)
      elseif dx >= kSwipePx then
        turn_page(-1)
      end
    end
    gesture = touch.gesture()
  end

  if want_rows then
    want_rows = false
    build_rows()
    apply_doors()
    ui.present(kCheapMode)
  end

  -- **The copy advances here and nowhere else**: a few blocks per turn, and the modal is painted only when what it says has
  -- changed by a step, because a repaint per block would be a screen that spends its life refreshing (docs/60).
  if task ~= nil then
    task.ticks = task.ticks + 1
    task_step()
    if task ~= nil then
      local key = task_key(task)
      if key ~= task.shown and (task.ticks >= 12 or task.phase == 'copy' and task.job == nil) then
        task.shown = key
        task.ticks = 0
        paint_modal(task)
        ui.present(kCheapMode)
      end
    end
  end

  sys.delay(20):await()
end
