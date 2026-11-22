-- ============================================================================
--  lunokOS :: Password
-- ============================================================================
--  A VAULT OF PASSWORDS: ONE ENCRYPTED FILE, ONE MASTER PASSWORD, AND FIVE
--  FIELDS PER ENTRY —title, user, password, web and note—.
--
--  **IT IS THE PASSWORD OF THE PAPER PROJECT (1.2.1) AND IT IS THE SAME APP** (the owner, 23/11: *«Farem la de password;
--  revisa el codi original a fons i fes-la igual»*): the same screens, the same words, the same five fields, the same file
--  format —a vault written by the Paper opens here and the other way round— and the same four doors of the system, with
--  `paperos.crypto` become `crypto` and `paperos.storage` become the one tree of `fs` (`docs/45`).
--
--    THE LOCK     The app opens LOCKED, always. A box of the SYSTEM takes the master password —with its cursor, its caret
--                 and the keyboard that comes up when it is touched, exactly like the box of the `Editor`— and the button
--                 `Open` reads the file. It is a SECRET box: one small filled dot per letter and never the letter
--                 (`mask`, the `set_secret` of the old project), and the script still reads the text whole. With no file
--                 yet, the same screen asks for the password TWICE and then makes one.
--
--    THE LIST     The entries, by title, with the user and the web underneath, in the system's list: the dial walks them,
--                 the centre opens the row it is on and a finger opens the one it touches. `New` writes an entry and
--                 `Lock` closes the vault. The count is in the bar of the system.
--
--    THE ENTRY    Title, user, password, web and note as lines of text. The password is masked until `Show` is pressed.
--                 `Edit` opens the form and `Delete` asks with the SYSTEM'S CARD —`Delete` or `Keep`— and never twice.
--
--    THE FORM     ONE field per page, in a box of the system: the cursor is there because the box is the system's and not a
--                 drawing of mine. `Next` goes to the field after it and on the last one it is `Save`; `Back` goes to the
--                 one before and on the first one it is `Cancel`.
--
--  **THE SCREENS ARE THE SYSTEM'S CONTROLS AND NOT A DRAWING OF MINE**, and that is the lesson the old project paid for
--  twice: a hand-painted screen has no cursor —the caret and the blink belong to the box— and no focus, and it has to mark
--  its own damage, and a rectangle marked by hand is a rectangle that can be one row off. Here a screen is **built** when it
--  arrives and **given back** when it leaves (`release()`), which is the one place this port differs from the old file: there
--  the pool of controls was fixed and the four screens were built once and hidden, and here a control costs nothing while
--  it exists and the memory is the only ceiling (docs/30, docs/90).
--
--  **AND THE SECRET IS NEVER ANYTHING BUT A LOCAL.** The key, the salt and the entries are locals of this machine, and the
--  app asks for **no session** —`sys.session` is the one door that keeps a piece of an app from one run to the next, and a
--  master password or a key in a block of the disk would be a master password and a key in the clear (docs/30). The machine
--  is closed when another app comes to the front, so **the vault comes back LOCKED every time, and that is not a
--  limitation: it is what a password manager has to do.**
--
--  **THE PAPER IS CLEARED BEFORE THE TREE**: every screen starts with `ui.clear(15)`, because the canvas keeps the ink of the
--  screen before and a page that does not erase it comes out with two texts on top of one another —which is exactly what the
--  owner saw as *«es dibuixa malament»* in the old project— (docs/30, docs/50).
--
--  **AND THE QUALITY OF THE PAINT IS THE SYSTEM'S** (docs/30, 0.179.5): a screen arrives with a `present` that the kernel
--  serves as a pass that cleans —it counts the passes and escalates to `gc16` when the panel has had too many— and a letter
--  typed into a box is served with the pass of the box. The old file had to ask for the `Gc16` itself
--  (`paperos.display.repaint()`), and here that decision is the kernel's, which is where the ghost of the whole glass is
--  known (docs/50, docs/92).
-- ============================================================================

local caps = sys.caps()
if not caps.display then
  console.print('password: this board has no glass')
  return
end

-- The file and the format are one module and the screens are this one: `vault.lua` knows a file, a key and five fields and

local vault = require('vault')

-- ---------------------------------------------------------------- the numbers of the page

local kMargin = 12          -- the margin of the glass, the twelve of the house (docs/92)
local kRowHeight = 68       -- a row of the list: two lines of words and their air
local kButtonHeight = 56    -- the height of a button of the system
local kBoxHeight = 72       -- a field is a line with its frame and the room of the caret
local kTitleScale = 3       -- 33 px of line with the face of the outline font of the disk
local kSmallScale = 2       -- 22 px: the line under a name and the body of an entry

-- A board with a finger brings the keyboard itself when the box is touched; a board with three keys has to open it, and
-- `touch.caps()` is the question (docs/30, docs/50).
local function fingers()
  local ok, glass = pcall(touch.caps)

  if not ok or type(glass) ~= 'table' then return 0 end

  return tonumber(glass.points) or 0
end

local kFingers = fingers()

-- ---------------------------------------------------------------- the state
--
-- ALL OF IT IS LOCAL, AND THAT IS A SECURITY DECISION (see the header).

local screen = 'locked'     -- locked | new | list | detail | edit
local state = nil           -- the open vault: key, salt, iterations, entries, on_card
local first = nil           -- the first of the two passwords of a new vault
local typed = ''            -- what has been typed on the lock screen, kept because the page is built again
local problem = ''          -- the last thing that did not work, in words
local rows = {}             -- the entries, in the order the list shows them
local shown = 0             -- the entry open on the entry screen
local edit = nil            -- the entry being written
local field = 1             -- the field the form is on (1..5)
local reveal = false        -- is the password on the glass?
local read_only = false     -- can this vault be changed? (it is not on the card)
local asking = false        -- is the card of the delete waiting for an answer?

-- The controls. None of them exists until a screen is built, and every one is a `local`: a control is not state and cannot
-- survive a stopped app.
local wid, hei = 0, 0
local page = nil            -- the panel of the screen on the glass
local pending = nil         -- a thing to do at the end of the turn, outside the callback that asked for it
local keyboard = nil        -- the band of the system, asked once
local lock_box = nil        -- the box of the master password
local form_content = nil    -- the panel of the form, which is not built again to move a field
local form_head = nil
local form_box = nil
local form_back = nil
local form_next = nil

-- **The screens of this app, named before they exist**: a button of one calls the next one and every one of them is a local
-- of this file, which is what keeps the secrets out of the globals (docs/30).
local show_lock, show_list, show_entry, show_form
local submit_lock, lock_cancel, enter_list, lock_now, open_selected
local start_new, start_edit_selected, start_edit, previous_field, next_field
local cancel_edit, save_edit, leave_to, ask_delete, do_delete

-- ============================================================ small tools

local function say(text)
  -- The system's log, and never a secret: what is printed is a count, the name of a screen or a reason the system gave.
  console.print('[PASSWORD] ' .. tostring(text))
end

--- The reason of the system in words. Every door of this system says no with a table `{code, message}` and a page that
--- shows the table shows nothing a person can read (`docs/30`).
local function why_text(err)
  if type(err) == 'table' then return tostring(err.message or err.code or 'it could not be done') end
  if err == nil then return 'it could not be done' end

  return tostring(err)
end

--- Something did not work, said on the screen AND in the log: the line over the buttons of the screen it happened on.
local function warn(text)
  problem = tostring(text)
  say(problem)
end

--- A text cut to what a line can hold, with three dots: a label of this system ends a long text in "..." and a heading has
--- to show that there is more of it.
local function clip(text, max)
  text = tostring(text or '')

  if max < 4 or #text <= max then return text end

  return text:sub(1, max - 3) .. '...'
end

-- ============================================================ the paragraph

--- **A paragraph is as many lines as it needs, measured with the face the page uses.** A label of this system is one line
--- and a long text ends in "..." (`docs/30`), and the words of this app —the explanation of the lock, a note of an entry—
--- are sentences: they are broken here, on the spaces, and every line is a label of its own.
local function lines_of(text, width, scale)
  local out = {}

  for paragraph in (tostring(text or '') .. '\n'):gmatch('([^\n]*)\n') do
    if paragraph == '' then
      out[#out + 1] = ''
    else
      local line = ''
      for word in paragraph:gmatch('%S+') do
        local try = (line == '') and word or (line .. ' ' .. word)
        if line == '' or ui.text_width(try, scale) <= width then
          line = try
        else
          out[#out + 1] = line
          line = word
        end
      end
      out[#out + 1] = line
    end
  end

  return out
end

local function a_word(parent, width, text, scale, align)
  local label = ui.label(width, scale * ui.line_height(1) + 4)
  if label == nil then return nil end
  label.text = text or ''
  label.scale = scale
  label.align = align or 'left'
  label.color = 0
  label.background = 15
  parent:add(label)

  return label
end

local function a_paragraph(parent, width, text, scale)
  for _, line in ipairs(lines_of(text, width, scale)) do
    a_word(parent, width, line, scale, 'left')
  end
end

local function a_slack(parent, width)
  local slack = ui.panel(width, 10, 'vertical')
  if slack == nil then return nil end
  slack.weight = 1
  parent:add(slack)

  return slack
end

local function a_button(parent, width, text, on_click)
  local button = ui.button(width, kButtonHeight)
  if button == nil then return nil end
  button.background = 15
  button.border = 0
  -- **The action runs at the end of the turn and not inside the callback**: a screen that changes gives its controls back,
  -- and the control that is running its own `on_click` is one of them (docs/30, docs/90).
  button.on_click = function() pending = on_click end
  local words = ui.label(width - 24, kTitleScale * ui.line_height(1) + 4)
  if words ~= nil then
    words.text = text
    words.scale = kTitleScale
    words.align = 'center'
    words.color = 0
    words.background = 15
    words:place(12, 4)
    button:add(words)
  end
  parent:add(button)

  return button
end

--- The text of a button, which is a label inside it and not a field of the control.
local function set_button_text(button, text)
  if button == nil then return end
  local words = button.children[1]
  if words ~= nil and words.text ~= text then words.text = text end
end

--- The band of the system, asked once.
local function the_keyboard()
  if keyboard == nil then
    local ok, band = pcall(ui.keyboard)
    if ok then keyboard = band end
  end

  return keyboard
end

--- **The band of a board with no finger is opened by the app**, because there is nothing to touch: a board with a finger
--- brings it when the box is touched, which is the old project's rule and the one the whole system follows (docs/30).
local function settle_the_band(box)
  local band = the_keyboard()
  if band == nil then return end

  if box ~= nil then band.target = box end
  if kFingers == 0 and box ~= nil then
    pcall(function() band:show('alpha') end)
  end
end

-- ============================================================ the page

--- Which page is on the glass, in one place: the bar's title and every screen kept in step.
local function apply()
  local title = 'Password'

  if screen == 'list' then
    title = ('%d %s'):format(#rows, (#rows == 1) and 'entry' or 'entries')

    -- The bar is where a person looks to know what they are looking at, and "read-only" is the one thing about this screen
    -- that changes what can be done in it.
    if read_only then title = title .. ' read-only' end
  elseif screen == 'detail' then
    local e = rows[shown]
    if e ~= nil then title = clip(vault.label(e), 32) end
  elseif screen == 'edit' then
    title = ('%s (%d of %d)'):format(vault.FieldNames[field], field, #vault.Fields)
  elseif screen == 'new' then
    title = (first == nil) and 'New vault' or 'Repeat it'
  end

  pcall(ui.title, title)
end

-- **A screen is given back before the next one is built, and giving back the page gives back its whole tree**
-- (`ui::release_slot`: a child whose father is gone could never be composed), o sigui que les files de la llista, els botons
-- i les caixes se'n van d'una mà i aquesta app no ha de guardar cap llista de controls per tornar-los (docs/30, docs/90).
local function clear_screen()
  if page ~= nil then page:release() end
  page = nil
  form_content, form_head, form_box, form_back, form_next = nil, nil, nil, nil, nil
  lock_box = nil
end

local function new_page()
  clear_screen()

  ui.clear(15)

  page = ui.panel(wid, hei, 'overlay')
  if page == nil then return nil end
  page:place(0, 0)

  local content = ui.panel(wid, hei, 'vertical')
  if content == nil then return nil end
  content.padding = kMargin
  content.gap = 6

  -- **La barra del sistema no es reserva** (0.190.258): `ui.caps()` diu que la vista d'una app comença a la fila 48
  -- —`view y` és 48 i l'alçada és 912—, o sigui que el canvas de l'app ja és sota la barra i el marge de dotze és tot el
  -- que hi ha d'haver a sobre del primer control. `files` hi posava una fila de barra sencera i quedava quaranta-set píxels
  -- d'aire que ningú no havia demanat (mesurat a l'emulador, 23/11).
  page:add(content)
  ui.root():add(page)

  return content
end

local function a_problem_line(parent, width)
  if problem == '' then return end

  a_paragraph(parent, width, problem, kSmallScale)
end

-- ---------------------------------------------------------------- the lock

--[[
  THE LOCK, WHICH IS ALSO THE SCREEN OF A NEW VAULT.

  The same page with three states —`Locked`, `New vault`, `Repeat it`— because it is the same question: a master password and
  one door. `Open` reads the file, `Create` makes the first one and `Repeat` confirms it, and the words of each state are the
  old project's, one by one.
]]
show_lock = function()
  local content = new_page()
  if content == nil then return end

  local inner = wid - 2 * kMargin

  if screen == 'new' then
    if first == nil then
      a_word(content, inner, 'New vault', kTitleScale, 'left')

      -- A VAULT LIVES ON THE CARD, SO THE SCREEN SAYS IT BEFORE ANYBODY TYPES a master password it would have nowhere to put.
      if not vault.card() then
        a_paragraph(content, inner,
                    'There is no memory card. A vault is kept on the card and nowhere else: put one in and come back.',
                    kSmallScale)
      else
        a_paragraph(content, inner,
                    'Choose a master password. It is the only key to this file: nobody can open it without it, and there is '
                    .. 'nothing to recover if it is forgotten.', kSmallScale)
      end
    else
      a_word(content, inner, 'Repeat it', kTitleScale, 'left')
      a_paragraph(content, inner,
                  'The same master password, once more. A password typed wrong once and not repeated is a vault nobody can '
                  .. 'open.', kSmallScale)
    end
  else
    a_word(content, inner, 'Locked', kTitleScale, 'left')
    a_paragraph(content, inner, 'The file is encrypted. Type the master password to read it.', kSmallScale)
  end

  a_problem_line(content, inner)

  -- **The paper that pushes the field and its door to the bottom of the page**, which is where the old screen had them: a
  -- panel with a weight takes what is left over (docs/30).
  a_slack(content, inner)

  -- **A PASSWORD IS WRITTEN IN A PASSWORD BOX**: `mask` paints one dot per letter and this screen keeps reading the whole
  -- text, which is exactly what the old `set_secret` was for (docs/30).
  lock_box = ui.textbox(inner, kBoxHeight)
  if lock_box ~= nil then
    lock_box.mask = true
    lock_box.scale = kTitleScale
    lock_box.text = typed
    lock_box.cursor = #typed
    lock_box.on_change = function(self, text)
      typed = text
      ui.present()
    end
    lock_box.on_enter = function() pending = submit_lock end
    content:add(lock_box)
  end

  local door = 'Open'
  if screen == 'new' then door = (first == nil) and 'Create' or 'Repeat' end
  a_button(content, inner, door, submit_lock)
  a_button(content, inner, 'Cancel', lock_cancel)

  apply()
  ui.present()
  settle_the_band(lock_box)
end

-- ---------------------------------------------------------------- the list

-- A row of the list: the name and, under it, the user and the web. **A row is a control with its own `on_click`**, which is
-- how the finger opens the entry it touched and how the dial's centre opens the one it is on (docs/30, docs/90).
local function add_row(the_list, width, index)
  local e = rows[index]
  if e == nil then return end

  local row = ui.list_entry(width, kRowHeight)
  if row == nil then return end
  row.background = 15

  local mine = rows

  -- **El retall el fa el label** (`docs/30`: una línia que no hi cap s'acaba amb `...`), o sigui que aquí no es torna a
  -- tallar: dos talls són dos punts suspensius i un títol que semblava més llarg del que és.
  a_word(row, width - 12, vault.label(e), kTitleScale, 'left')

  local under = a_word(row, width - 12, vault.subtitle(e), kSmallScale, 'left')
  if under ~= nil then under:place(0, 5 + kTitleScale * ui.line_height(1)) end

  row.on_click = function()
    -- A row of a list that has been built again acts on nothing: the table of this page is not the one the row was made for.
    if rows ~= mine then return end
    pending = function() open_selected(index) end
  end

  the_list:add(row)
end

show_list = function()
  local content = new_page()
  if content == nil then return end

  local inner = wid - 2 * kMargin

  if #rows == 0 then
    -- A list with no rows takes no space and the page would be paper with two buttons, which reads as "broken" and not as
    -- "there is nothing here yet" (the old screen's lesson, word for word).
    a_paragraph(content, inner, 'The vault is empty. New makes the first entry.', kSmallScale)
    a_slack(content, inner)
  else
    local the_list = ui.list(inner, kRowHeight)
    if the_list ~= nil then
      the_list.weight = 1
      the_list.row_height = kRowHeight
      the_list.separator = true
      content:add(the_list)
      -- **The rows are built for the whole list and the control pages them itself**: it places the ones of the page it is
      -- on, turns with the dial and with a finger, and paints the paginator of the system at its foot —one paginator, and
      -- not the app's over the control's one, which is the bug the board found in `files` (docs/30, docs/90).
      for i = 1, #rows do
        add_row(the_list, inner, i)
      end
    end
  end

  a_problem_line(content, inner)

  local buttons = ui.panel(inner, kButtonHeight, 'horizontal')
  if buttons ~= nil then
    buttons.gap = kMargin
    content:add(buttons)
    local half = (inner - kMargin) // 2
    a_button(buttons, half, 'New', start_new)
    a_button(buttons, half, 'Lock', lock_now)
  end

  apply()
  ui.present()
  settle_the_band(nil)
end

-- ---------------------------------------------------------------- the entry

show_entry = function()
  local e = rows[shown]
  if e == nil then
    screen = 'list'
    show_list()
    return
  end

  local content = new_page()
  if content == nil then return end

  local inner = wid - 2 * kMargin

  a_word(content, inner, vault.label(e), kTitleScale, 'left')

  -- **The password is masked on the SCREEN until `Show`**: a screen that reads it out the moment the row is opened has
  -- already told the next person there.
  local pass = '-'
  if (e.pass or '') ~= '' then pass = reveal and e.pass or string.rep('*', #e.pass) end

  a_word(content, inner, 'User: ' .. (((e.user or '') ~= '') and e.user or '-'), kSmallScale, 'left')
  a_word(content, inner, 'Password: ' .. pass, kSmallScale, 'left')
  a_word(content, inner, 'Web: ' .. (((e.web or '') ~= '') and e.web or '-'), kSmallScale, 'left')
  a_paragraph(content, inner, 'Note: ' .. (((e.note or '') ~= '') and e.note or '-'), kSmallScale)

  a_slack(content, inner)
  a_problem_line(content, inner)

  local top = ui.panel(inner, kButtonHeight, 'horizontal')
  if top ~= nil then
    top.gap = kMargin
    content:add(top)
    local half = (inner - kMargin) // 2
    -- **Aquestes dues funcions ja corren fora del `on_click` del botó** —`a_button` les posa a `pending`, que el bucle
    -- executa al torn següent—, o sigui que poden tornar a fer la pàgina elles mateixes: un salt més seria una passada
    -- sencera de pintura per una paraula (docs/30, docs/90).
    a_button(top, half, 'Back', function() leave_to('list') end)
    a_button(top, half, reveal and 'Hide' or 'Show', function()
      reveal = not reveal
      show_entry()
    end)
  end

  local bottom = ui.panel(inner, kButtonHeight, 'horizontal')
  if bottom ~= nil then
    bottom.gap = kMargin
    content:add(bottom)
    local half = (inner - kMargin) // 2
    a_button(bottom, half, 'Edit', start_edit_selected)
    a_button(bottom, half, 'Delete', ask_delete)
  end

  apply()
  ui.present()
  settle_the_band(nil)
end

-- ---------------------------------------------------------------- the form

--- Puts the field the form is on into the box, says which one it is and names the two doors.
local function load_field()
  if form_box ~= nil then
    local value = (edit ~= nil and edit[vault.Fields[field]]) or ''
    form_box.text = value
    form_box.cursor = #value
    -- **AND THE FIELD OF THE PASSWORD IS A PASSWORD TOO**: the vault shows it to nobody until `Show`, and this box does the
    -- same while it is being typed (the old `set_secret(key == 'pass')`, word for word).
    form_box.mask = (vault.Fields[field] == 'pass')
  end
  if form_head ~= nil then
    form_head.text = ('%s  (%d of %d)'):format(vault.FieldNames[field], field, #vault.Fields)
  end
  set_button_text(form_back, (field > 1) and 'Back' or 'Cancel')
  set_button_text(form_next, (field >= #vault.Fields) and 'Save' or 'Next')
end

--- **A field of the form is one screen, and moving to the next one is not a new screen**: the box is the system's and it
--- keeps its cursor, so the text and the two doors are written on the controls that are already there. Only the arrival on
--- the form —and a view that changed size— builds it (`first_time`).
show_form = function(first_time)
  if first_time or page == nil then
    local content = new_page()
    if content == nil then return end
    form_content = content

    local inner = wid - 2 * kMargin

    form_head = a_word(content, inner, '', kTitleScale, 'left')
    a_slack(content, inner)

    form_box = ui.textbox(inner, kBoxHeight)
    if form_box ~= nil then
      form_box.scale = kTitleScale
      form_box.on_change = function(self, text)
        if edit ~= nil then edit[vault.Fields[field]] = text end
        ui.present()
      end
      form_box.on_enter = function() pending = next_field end
      content:add(form_box)
    end

    a_problem_line(content, inner)

    local buttons = ui.panel(inner, kButtonHeight, 'horizontal')
    if buttons ~= nil then
      buttons.gap = kMargin
      content:add(buttons)
      local half = (inner - kMargin) // 2
      form_back = a_button(buttons, half, 'Cancel', previous_field)
      form_next = a_button(buttons, half, 'Next', next_field)
    end
  end

  load_field()
  apply()
  ui.present()
  settle_the_band(form_box)
end

-- ================================================================ the flow

local function refresh()
  rows = {}

  if state ~= nil then
    for i = 1, #state.entries do rows[i] = state.entries[i] end
  end

  vault.sort(rows)

  if shown > #rows then shown = #rows end
end

--- The index of an entry in the vault's own order, BY IDENTITY: the list is a sorted copy of the same tables, so `rows[i]`
--- and `state.entries[j]` can be the same table at two different places.
local function entry_index(target)
  if state == nil or target == nil then return nil end

  for i = 1, #state.entries do
    if state.entries[i] == target then return i end
  end

  return nil
end

--- Can this vault be changed? If it is not on the card, no, and it says why.
---
--- The guard is HERE and in front of the three things that would write —New, Edit and Delete— because a vault that is not
--- on the card must not be changed in any way, and a screen that hides a button is a screen whose rule lives in the button.
local function can_change()
  -- **La pregunta es fa al fitxer i no al número que es va llegir a l'obrir** (0.190.258): una targeta que surt del lector
  -- a mig camí és `editable()` que ho diu, i la barra ho ha de dir també el cop següent. El vell comprovava el seu propi
  -- `readOnly` i la targeta que marxava només es veia al `save()`, que refusava DESPRÉS que l'entrada hagués sortit de la
  -- memòria (docs/45, docs/70).
  read_only = not vault.editable(state)

  if not read_only then return true end

  say('refused: this vault is not on the card, it opens read-only')
  warn('This vault is read-only: it is not on the memory card')

  return false
end

submit_lock = function()
  if screen == 'locked' then
    if typed == '' then
      warn('Type the master password first')
      show_lock()
      return
    end

    local opened, why = vault.unlock(typed)

    if opened == nil then
      warn(why)
      say('locked (that password did not open it)')
      typed = ''
      show_lock()
      return
    end

    state = opened
    problem = ''
    typed = ''
    say(('unlocked %d entries'):format(#state.entries))
    enter_list()
    return
  end

  if screen ~= 'new' then return end

  -- **El botó `Create` no gasta la contrasenya mestra si el vault no pot existir** (the old `Password.md`: *«el botó `Create`
  -- no fa res i diu el motiu»*): preguntar-la dues vegades per a un fitxer que no es pot escriure és un minut de la persona
  -- i una frase que no espera. Les dues regles són les de `vault.create()`, i allà es tornen a comprovar (docs/45, docs/70).
  if first == nil and not vault.card() then
    warn('a vault is kept on the memory card and nowhere else: put one in first')
    show_lock()
    return
  end
  if first == nil and vault.OpenPath ~= nil then
    warn('this screen opens a file, it does not make one')
    show_lock()
    return
  end

  if typed == '' then
    warn('Type a master password first')
    show_lock()
    return
  end

  if first == nil then
    first = typed
    typed = ''
    problem = ''
    show_lock()
    return
  end

  if typed ~= first then
    first = nil
    typed = ''
    warn('The two passwords are not the same: start again')
    show_lock()
    return
  end

  local made, why = vault.create(typed)
  first = nil
  typed = ''

  if made == nil then
    warn(why)
    show_lock()
    return
  end

  state = made
  local saved, save_why = vault.save(state)

  if not saved then
    warn('the new vault could not be written: ' .. why_text(save_why))
  else
    say('made a vault')
  end

  enter_list()
end

lock_cancel = function()
  if first ~= nil then
    first = nil
    typed = ''
    problem = ''
    show_lock()
    return
  end

  -- From the lock there is nothing to cancel: the app leaves, and the machine closes with the key inside it.
  app.exit()
end

enter_list = function()
  screen = 'list'
  problem = ''
  reveal = false

  -- WHERE THE VAULT IS DECIDES WHAT CAN BE DONE IN IT, and it is asked once per unlock: a card taken out mid-session is
  -- caught again by `can_change()` and by `vault.save()`.
  read_only = not vault.editable(state)

  if read_only then
    say('read-only: the vault is not on the card')
  end

  refresh()
  show_list()
end

open_selected = function(index)
  if rows[index] == nil then return end

  shown = index
  reveal = false
  screen = 'detail'
  show_entry()
end

start_edit_selected = function()
  start_edit(rows[shown])
end

start_new = function()
  shown = 0
  start_edit(nil)
end

start_edit = function(target)
  if not can_change() then
    screen = 'list'
    show_list()
    return
  end

  edit = vault.normalize(target or {})
  field = 1
  screen = 'edit'
  problem = ''
  reveal = false

  say('editing field 1 of ' .. #vault.Fields)
  show_form(true)
end

previous_field = function()
  if field > 1 then
    field = field - 1
    show_form(false)
    return
  end

  cancel_edit()
end

next_field = function()
  if field < #vault.Fields then
    field = field + 1
    show_form(false)
    return
  end

  save_edit()
end

cancel_edit = function()
  edit = nil
  problem = ''
  screen = 'list'
  refresh()
  show_list()
end

save_edit = function()
  if edit == nil or state == nil then return end

  if not can_change() then
    screen = 'list'
    show_list()
    return
  end

  if vault.is_empty(edit) then
    warn('an entry with nothing in it is not saved')
    -- **La línia del motiu és un control de la pàgina**: una pàgina que no es torna a fer no la pot ensenyar, i el formulari
    -- és l'únic que s'escriu sobre si mateix per no perdre el cursor (docs/30).
    show_form(true)
    return
  end

  local target = vault.normalize(edit)
  local index = (shown > 0) and entry_index(rows[shown]) or nil

  if index ~= nil then
    state.entries[index] = target
  else
    table.insert(state.entries, target)
  end

  local saved, why = vault.save(state)

  if not saved then
    warn('not saved: ' .. why_text(why))
    show_form(true)
    return
  end

  say(('saved %d entries'):format(#state.entries))

  edit = nil
  screen = 'list'
  problem = ''
  refresh()

  -- And the row of the entry that was just saved is the one that opens, wherever the sort has put it.
  shown = 0
  for i = 1, #rows do
    if rows[i] == target then shown = i end
  end

  show_list()
end

leave_to = function(where)
  screen = where
  problem = ''
  if where == 'list' then
    refresh()
    show_list()
  end
end

lock_now = function()
  state = nil
  edit = nil
  first = nil
  typed = ''
  rows = {}
  shown = 0
  reveal = false
  problem = ''
  screen = vault.exists() and 'locked' or 'new'
  say('locked')
  show_lock()
end

--- **`Delete` ASKS WITH THE SYSTEM'S CARD** (`ui.dialog`): it takes the dial and the finger while it is alive, a tap outside
--- it is its own cancel, and the sentence wraps instead of ending in "...". It is the old project's `ui.menu`, and the door
--- of default is `Keep`: what is about to be lost is not the finger's first answer (docs/30, docs/90).
ask_delete = function()
  if rows[shown] == nil or state == nil then return end
  if not can_change() then
    show_entry()
    return
  end

  local question = ui.dialog(wid - 2 * kMargin, 300)
  if question == nil then return end

  question.title = 'Delete this entry?'
  question.text = vault.label(rows[shown]) .. ': it cannot be undone.'
  question.accept = 'Delete'
  question.cancel = 'Keep'
  question.emphasis = 1
  question:place(kMargin, (hei - 300) // 2)
  question.on_accept = function(self)
    asking = false
    self:release()
    pending = do_delete
  end
  question.on_cancel = function(self)
    asking = false
    self:release()
    pending = show_entry
  end

  asking = true
  ui.root():add(question)
  ui.present()
end

do_delete = function()
  local target = rows[shown]

  if target == nil or state == nil then
    show_list()
    return
  end

  local index = entry_index(target)

  if index ~= nil then table.remove(state.entries, index) end

  local saved, why = vault.save(state)

  if not saved then
    warn('not deleted: ' .. why_text(why))
    show_entry()
    return
  end

  say(('deleted one, %d entries'):format(#state.entries))

  screen = 'list'
  shown = 0
  problem = ''
  refresh()
  show_list()
end

-- ======================================================= the app

--- **The file this run is about, when `Files` opens one**: the browser starts this app with the path of the file it touched
--- (`app.run(id, back, flags, path)`), and the prompt's authority over that one path is what makes it writable even on the
--- card (docs/30, docs/70).
local function read_args()
  if app.args ~= nil and app.args.path ~= nil and app.args.path ~= '' then return app.args.path end

  return nil
end

vault.OpenPath = read_args()

local view = ui.caps()
wid, hei = view.width, view.height

if vault.OpenPath ~= nil then
  say('opening ' .. vault.OpenPath)
end

-- **A vault that is not on the card is read-only, and the person is told before they type anything**: a master password
-- written for a file that cannot be saved is a minute of their life and a sentence they do not expect (docs/45).
if not vault.card() then
  say('there is no card: a vault cannot be made, and one already made opens read-only')
end

screen = vault.exists() and 'locked' or 'new'
say(screen == 'locked' and 'locked' or 'no vault yet: a new one is made')

show_lock()

-- ---------------------------------------------------------------- the loop

while true do
  -- **The room of the view is what the page is built from**: the band of the keyboard comes and goes and changes it, and the
  -- system can give the app another view (docs/30, docs/40).
  local now = ui.caps()
  if now.width ~= wid or now.height ~= hei then
    wid, hei = now.width, now.height
    if screen == 'edit' then
      show_form(true)
    elseif screen == 'list' then
      show_list()
    elseif screen == 'detail' then
      show_entry()
    else
      show_lock()
    end
  end

  if pending ~= nil then
    local doing = pending
    pending = nil
    doing()
  end

  -- **The gestures of the system are taken and not left in the queue**: the list turns its own pages and this app has no
  -- gesture of its own, so what arrives here is read and dropped (docs/30).
  if kFingers > 0 then
    local g = touch.gesture()
    while g do
      g = touch.gesture()
    end
  end

  sys.delay(20):await()
end
