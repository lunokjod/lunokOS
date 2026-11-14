-- home_eink: **the launcher of the glass** (0.153.0, and 0.154.0 with the page that arrives before its pictures).
--
-- The apps of the disk, in a grid of pictures and names: a finger on a picture starts the app it belongs to. The list is
-- not read from `/apps` by this file —**the system reads it**—, because the door of the apps already puts the flash and
-- the card together and refuses a folder that is not an app (docs/40).
--
-- **The page comes first and the pictures after it, one app at a time** (0.154.0): `apps.list()` answers the names and
-- opens no manifest, so the grid is drawn at once with the name of each app and a **generic picture** —a box with its
-- first letter—, and then a queue asks `apps.info(id)` for one app per turn and puts the real name and the real picture
-- in the cell that is already there. A page that waited for every manifest and every picture before showing anything
-- arrived late, and one app whose picture cannot be read held up the rest (docs/30).
--
-- **A launcher does not list itself, and no launcher lists another.** This one knows its own name (`app.id`) and leaves
-- it out before anything is read; an app that says `"launcher": true` in its manifest is taken out when its manifest
-- arrives, and the page is laid out again without it. It is not a hidden app: `apps` lists it, `run home_eink` starts
-- it, and the board comes up inside it (`cfg home home_eink`).
--
-- **The pages are the grid's, and its own footer is the paginator** (0.157.0): this file hands over **every** cell —all
-- the apps of the disk— and the grid keeps the page of the cell the dial is on and paints `N / M` at its foot when there
-- is more than one. That footer, and the cells of the other pages kept out of sight, are `icon_grid_cells`, the same
-- arithmetic the paint and the finger use (docs/90). **The cursor is the control's** (0.179.4): a cell that takes the ring
-- is the page the grid turns to, and the system writes `index` where the focus arrives, so this file has no `on_focus` and
-- no page of its own. **A paginator of its own was a control the system already has**: two arrows and a number under the
-- grid are gone (the owner, 8/11: *«usa sempre els UIControls que hi hagin abans de fer-ne un de nou»*).
--
-- **A press is a wish and not a switch**: `app.run(id)` answers at once and the kernel does the change after the turn,
-- because the app that asks is the one the interpreter belongs to (docs/30, docs/90). The second argument is the way
-- back: with it the person returns to this page when the app they opened ends, which is the only way back on a board
-- whose single way out of an app is a key.
--
-- **The sizes are the grid's own** and not this file's invention (docs/90): a cell of an `ui.icon_grid` with entries is
-- as wide as the grid divided by the columns, and its box is `icon + 8 + line + 8` high, where the icon is at most
-- seventy-two pixels. The picture and the name are placed inside that box, and a cell that does not fit its children
-- would paint them over the next cell.
--
-- **AND THE SECOND TIME THIS PAGE COMES UP IT COSTS NO DISK** (0.179.0): the manifests and the pictures of the cells are
-- kept in a **memory of the session** that the kernel holds for this app under the key `apps`, so a launcher that is left
-- and started again —which on this board is every time an app ends— draws its real names and its real pictures in the
-- first turn, and a dial that walks over a cell paints a copy of a picture it already decoded instead of reading the file
-- and decoding it again. The block is asked once (`sys.session`), it is zeroed when it is new —which is how this file tells
-- a cold cache from a warm one— and it lives until the board is switched off **or the memory runs short**: it is the first
-- thing the system gives back when a block does not fit, because it is the only thing it can make again (docs/30, docs/75).

local caps = sys.caps()
if not caps.display then
  console.print('home: this board has no glass')
  return
end

local view = ui.caps()

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the flash, so
-- the sizes of the page are asked in pixels and the face answers with the steps it has (docs/30).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local kName = scale_for(33)
local kNameLine = kName * ui.line_height(1)

-- **The box of a cell, in the arithmetic of `icon_grid_cells`** (docs/90): three columns make a cell wide enough for a
-- name, and the icon is the seventy-two pixels the grid gives a mark when the cell is not narrow.
local kColumns = 2
-- **The box of a picture is what the kernel computes for the cell** (0.166.1): a grid makes a mark as wide as its cell
-- minus the padding, and the app has to ask for exactly that or the picture sits small inside a big box. It is worked out
-- in `build()` because it depends on the width of the view.
local kIcon = 72
local kPad = 4

-- **A CELL IS NEW INK OVER PAPER, AND IT DOES NOT FLASH** (0.179.15): the picture and the letter of a cell are drawn
-- where the paper is clean, so there is no ghost to erase and nothing to wash —`gld16` lands the sixteen levels and
-- keeps its ink from blinking—. Without this the cell takes the quality of the *page*, which after thirty-two painted
-- boxes is `gc16`: four blinks for a picture nobody had painted before, ten cells at a time, every time the launcher
-- comes back (docs/30, docs/50).
local kCellMode = 'gld16'

-- **A row is the box of a cell** and the same number `icon_grid_cells` computes from the icon and the line (docs/90): the
-- cells are the app's and their box is what the picture and the name are placed in.
local kCellH = kIcon + 8 + kNameLine + 8

local kMargin = 20
local kTop = 8
-- **The strip of the loading bar** (0.189.15, the owner: *«la barra de progrés ... posa-la a dalt, sota la barra de sistema
-- (enganxada), ben fineta, semblarà un navegador web carregant un recurs i es entenedor per tothom»*): six pixels of box on
-- the very top of the page —which is the glass under the strip of the system, because the view of an app begins there
-- (docs/30)— and four of ink inside them, because the control keeps a pixel of air round its own fill. It is a line and
-- not a band: a bar as tall as a row says one number with the room of a footer (docs/90, docs/92).
local kBarH = 6

-- ------------------------------------------------------------------ the cache of the session

-- **What is kept, and where**: a header of eight bytes —the magic and the side of a picture, so a cache of another size of
-- cell is not painted on this one—, the records of the apps and then their pictures, four bits a pixel and in the order of
-- the block (`cv:capture` and `cv:blit` are a copy and not a decode). A record is `id`, the title, the icon, the number of
-- the picture and **the version of the manifest** (0.189.22), and the number is how this file knows whether a picture is in
-- the block or has to be read from the disk.
-- **The size asked for, and what it falls back to when the board cannot give it** (0.179.1): a block that holds the records
-- and no picture is still worth having —the manifests are what the queue pays for one per turn— and a block that holds
-- neither is not the end of the page. The pieces come down and the last one is the records alone —1 536 B, plus the eight of
-- the header— which is what a board with a crowd of apps or with the memory spent gets (docs/30, docs/75).
local kCacheSteps = { 32768, 24576, 16384, 8192, 1544 }
local kCacheMagic = 'HEC2'
local kCacheText = 8
local kCacheTextBytes = 1528
local kCachePictures = kCacheText + kCacheTextBytes

local cache = nil          -- the block, or nil when the system gave none
local cached = {}          -- the id of an app to { title=, icon=, slot=, version= }, as the block says
local cached_order = {}    -- the ids the block gave, in the order it gave them (the sorted order of the page)
local slots = 0            -- how many pictures the block holds
local warm = {}            -- the ids whose picture is in the block, so a paint is a copy

-- **The names of the door, the manifests that have arrived, and the queue between the two** (0.154.0). `known` is filled
-- as the queue runs, and a page laid out again uses it instead of asking the disk a second time.
local ids = {}
local known = {}
local cells = {}      -- the id of an app to its three controls, so a manifest can land in a cell that is already drawn
local queue = {}
local head = 1
-- **La segona cua: les versions** (0.189.22): the ids the block gave are asked the disk **one per turn**, after the queue that
-- fills the cells, so a page that came back from the block is still instant and the check of a version never holds a turn of
-- the glass. It is the same rhythm the manifests arrive at when the cache is cold, and the answer only costs a paint when the
-- number has moved (docs/30, docs/40).
local verify = {}
local vhead = 1
local again = false   -- something left the page, so it is laid out again
local grid = nil
-- **La línia del carregament** (0.189.15, abans 0.186.2): the queue of manifests and pictures takes one app a turn, and
-- while it runs this is the control that says how far it is. It is a thin line on the top of the page, flush under the
-- strip of the system —the place a browser puts it, and the one a person reads without being told— and it goes the moment
-- the queue is over.
local bar = nil

-- **How many times each app has been opened** (0.189.13): with `cfg sort on` the person asked this page to draw the apps
-- from the most opened to the least, and the count is this page's own memory —a session of its own, `usage`, and not the
-- `state` one, because a count is what the page has learned and not the shape of the view (docs/30, docs/75)—. It is
-- written on the tap and not when the app ends: a launch is counted even when the app cannot start, which is what a person
-- means by "I use this one".
local kUsageKey = 'usage'
local kUsageSteps = { 1024, 512, 256 }
local usage = nil            -- the block, or nil when the system gave none
local uses = {}              -- the id of an app to how many times it has been opened
local sort_by_usage = false  -- what `cfg sort` says, read once when the page starts

-- **What a picture takes in the block**, in bytes: four bits a pixel and the stride of the canvas the kernel makes.
local function picture_bytes()
  return ((kIcon + 1) // 2) * kIcon
end

-- **The block is asked once and read**: a cold one has no magic —the kernel gives it zeroed— and every record of it is
-- taken back into `cached`. A block whose side is not the side of this page is not read at all: the pictures in it were
-- measured for another cell. A record says whether its picture is really in the block, because an app whose picture failed
-- to be drawn has a number and no picture, and a zeroed box painted as an icon is a black square (docs/30).
local function load_cache()
  cache = nil
  cached = {}
  cached_order = {}
  slots = 0
  warm = {}
  local block, err = nil, nil
  for i = 1, #kCacheSteps do
    block, err = sys.session('apps', kCacheSteps[i])
    if block ~= nil then break end
  end
  if block == nil then
    console.print('home: no cache of the session: ' .. tostring(err and err.message or err))
    return
  end
  cache = block
  if cache:read(0, 4) ~= kCacheMagic then
    console.print(string.format('home: cache cold, %d bytes', cache:size()))
    return
  end
  if cache:read(4, 1):byte(1) ~= kIcon then
    console.print('home: cache of another size of cell, building it again')
    return
  end
  -- **The text ends at the first zero** (0.179.0): the block is read whole —the records are shorter than their window— and a
  -- zero is not in any of them, so what is after the last one is the kernel's zeroing and not a record with a broken tail
  -- (a launcher that lost the last app of its cache asked for it again at every start, which is how this was found).
  local text = cache:read(kCacheText, kCacheTextBytes)
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  local count = 0
  local drawn = 0
  -- **Un registre porta la versió** (0.189.22): `id`, the title a person reads, the path of the picture, the number of its
  -- picture in the block and **the version of the manifest it was read from**. Without that number the block cannot tell an
  -- app that has been updated from one that has not, and the icon and the title of the old version would outlive the app that
  -- wrote them —which is the one thing a cache of pictures cannot be allowed to do— (docs/30, docs/40).
  for line in text:gmatch('[^\n]+') do
    local id, title, icon, slot, has, version = line:match('^([^\t]+)\t([^\t]*)\t([^\t]*)\t(%d+)\t([01])\t([^\t]*)$')
    if id ~= nil then
      slot = tonumber(slot)
      cached[id] = { title = title, icon = icon, slot = slot, version = version }
      cached_order[#cached_order + 1] = id
      if slot + 1 > slots then slots = slot + 1 end
      if has == '1' then
        warm[id] = true
        drawn = drawn + 1
      end
      count = count + 1
    end
  end
  console.print(string.format('home: cache warm, %d apps, %d pictures', count, drawn))
end

-- **A block that the memory has taken is a block this file forgets** (0.179.8): the relief of the allocator gives every
-- session memory back the moment a block does not fit (`[I][MEMORY] home_eink gives back 'apps' … the memory is short`), and
-- a launcher that went on using it raised `the session memory was given back` inside every draw of a cell —four lines of
-- `present:` and a page of letters with no pictures, which is what the owner pasted at 0.179.6—. The question is asked
-- where the block is used and the answer is one word, and the turn after asks for another one: the page is worth more than
-- the cache, and a block of a thousand bytes with the names of the apps in it is still a block (docs/30, docs/75).
local lost = false
local function the_cache_is_ours()
  if cache == nil then return false end
  if cache:alive() then return true end
  console.print(string.format('home: the cache of the session was given back, %d app(s) in it', #cached_order))
  cache = nil
  cached = {}
  cached_order = {}
  warm = {}
  slots = 0
  lost = true
  return false
end

-- **And it is written when the page is whole**: the magic goes last, so a run that ends in the middle of the queue does not
-- leave a header that promises records the block does not have. A title or a path with the two characters of the format in
-- it is not written —a cache that breaks the parse is a cache that is never read again— and the app is asked next time.
local function save_cache()
  if not the_cache_is_ours() then return end
  local text = {}
  local slot = 0
  local held = 0
  for i = 1, #ids do
    local id = ids[i]
    local info = known[id]
    if info ~= nil then
      local title = info.title
      local icon = info.icon
      local version = info.version or ''
      if not title:find('[\t\n]') and not icon:find('[\t\n]') and not id:find('[\t\n]') and
         not version:find('[\t\n]') then
        local was = cached[id]
        if was ~= nil then
          slot = was.slot
        else
          slot = slots
          slots = slots + 1
        end
        cached[id] = { title = title, icon = icon, slot = slot, version = version }
        local has = warm[id] and '1' or '0'
        if has == '1' then held = held + 1 end
        text[#text + 1] = string.format('%s\t%s\t%s\t%d\t%s\t%s', id, title, icon, slot, has, version)
      end
    end
  end
  local body = table.concat(text, '\n')
  if #body > kCacheTextBytes then
    console.print('home: too many apps for the cache of the session')
    return
  end
  cache:write(kCacheText, body)
  cache:write(4, string.char(kIcon))
  cache:write(0, kCacheMagic)
  console.print(string.format('home: cache kept, %d apps, %d pictures', #text, held))
end

-- **And what the person has opened, which is the other thing this page remembers** (0.189.13): one line an app,
-- `id<TAB>count`, and the block is read whole —the text ends at the first zero, which is the kernel's zeroing and not a
-- line with a broken tail—. A count of an app that is no longer on the disk stays here and costs one line: the door is not
-- asked to prune a number that would come back the day the app does (docs/30, docs/75).
local function load_usage()
  usage = nil
  uses = {}
  local block, err = nil, nil
  for i = 1, #kUsageSteps do
    block, err = sys.session(kUsageKey, kUsageSteps[i])
    if block ~= nil then break end
  end
  if block == nil then
    console.print('home: no count of the uses: ' .. tostring(err and err.message or err))
    return
  end
  usage = block
  local text = usage:read(0, usage:size())
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  local count = 0
  for line in text:gmatch('[^\n]+') do
    local id, n = line:match('^([^\t]+)\t(%d+)$')
    if id ~= nil then
      uses[id] = tonumber(n) or 0
      count = count + 1
    end
  end
  if count > 0 then
    console.print(string.format('home: count of the uses, %d app(s)', count))
  end
end

-- **The order of the page, when the person asked for it** (0.189.13): from the most opened to the least, and the name
-- breaks a tie —so two apps with the same count, which is every app the first day, keep the order of the door and the page
-- does not move from one start to the next—. With the switch off the list is left as the door gave it (docs/40).
local function by_usage(names)
  if not sort_by_usage then return end
  table.sort(names, function(a, b)
    local opened, other = uses[a] or 0, uses[b] or 0
    if opened ~= other then return opened > other end
    return a < b
  end)
end

-- **A tap is one use, and it is written now** (0.189.13): the app the person chose may take the glass for good, so waiting
-- for the end of it would lose the count of the one launch that matters. The block is asked whether it is still ours,
-- because the relief of the allocator takes the sessions of an app when the memory is short and a write to a block that
-- was given back raises (docs/30, docs/75). The lines are written from the most used, so a block too small to hold them
-- all keeps the apps this page needs and drops the tail of once-opened ones.
local function save_usage()
  if usage == nil then return end
  if not usage:alive() then
    console.print('home: the count of the uses was given back')
    usage = nil
    return
  end
  local order = {}
  for id in pairs(uses) do order[#order + 1] = id end
  table.sort(order, function(a, b)
    local opened, other = uses[a] or 0, uses[b] or 0
    if opened ~= other then return opened > other end
    return a < b
  end)
  local room = usage:size() - 1
  local lines = {}
  local length = 0
  local left = 0
  for i = 1, #order do
    local line = string.format('%s\t%d', order[i], uses[order[i]])
    local need = #line + ((#lines > 0) and 1 or 0)
    if length + need > room then
      left = #order - i + 1
      break
    end
    lines[#lines + 1] = line
    length = length + need
  end
  if left > 0 then
    console.print(string.format('home: %d app(s) do not fit in the count of the uses', left))
  end
  -- **The zero that closes the text**: a shorter list written over a longer one would leave the tail of the old one in the
  -- block, and a block is read whole (docs/30).
  usage:write(0, table.concat(lines, '\n') .. '\0')
end

local function bump_usage(id)
  uses[id] = (uses[id] or 0) + 1
  save_usage()
end

-- ------------------------------------------------------------------ the page

local function give_back()
  if bar ~= nil then
    bar:release()
    bar = nil
  end
  if grid ~= nil then
    grid:release()
    grid = nil
  end
  cells = {}
end

-- **How far the queue is, in per cent**: the turns it has taken out of the ones it has. A queue with nothing in it is a
-- page that has nothing to wait for, and that is 100 before the first cell.
local function queue_percent()
  local total = #queue
  if total == 0 then return 100 end
  local done = head - 1
  if done < 0 then done = 0 end
  if done > total then done = total end
  return math.floor(done * 100 / total)
end

-- **The bar is the top of the page and it is a line** (0.189.15): as wide as the glass, six pixels tall, flush under the
-- strip of the system and filled with ink —which is how a browser says "this is still arriving"—. It used to sit in the band
-- of the paginator, thirty-seven pixels of a control at the foot, and the owner asked for this instead: the bar of a page
-- that is loading is the thin line everybody has already learned to read, and the foot belongs to the dots (docs/30,
-- docs/90).
--
-- It is a sibling of the grid and not a child of it, because a grid counts its children as cells (docs/30, docs/90).
local function show_the_bar(width)
  if head > #queue then return end
  if bar == nil then
    bar = ui.progress(width, kBarH)
    if bar == nil then return end
    -- **The fill is the ink and the track is the paper** (0.189.15): the bar of the system fills with the light tone of
    -- the palette, which is right in a box of twenty-eight pixels and invisible in a line of six. A dark line that grows
    -- is what a person reads as "loading", and the ink of a strip six pixels tall and as wide as the glass is a few
    -- thousand pixels and not the hundred thousand of a black box (docs/50, docs/92).
    bar.fill = 0
    bar.track = 15
    bar.border = 15
    bar.background = 15
  else
    bar:resize(width, kBarH)
  end
  bar.value = queue_percent()
  -- **Flush under the strip of the system**: the view of an app is the glass under the bar, so zero is the row the system
  -- stops at (docs/30).
  bar:place(0, 0)
end

-- **I la barra marxa quan la cua s'ha acabat** (0.186.2): the release fills the hole it leaves with paper and asks what
-- was under it to draw —and what was under it is the paper above the grid, because the grid begins at `kTop` and the strip
-- is thinner than that—. No cell of the grid is repainted, then: the foot with the dots of the paginator is the grid's
-- business and was never this strip's (docs/30, docs/90).
local function finish_the_bar()
  if bar == nil then return end
  bar:release()
  bar = nil
end

-- **A picture that could not be drawn is said once** (0.179.0, and it was said at the arrival of the manifest before):
-- the paint is the one that knows —it is the one that reads the file— and a page that says it at every turn says it every
-- time the dial walks over the cell (docs/30, docs/90).
local said = {}
local function say_the_fault(id, why)
  if said[id] then return end
  said[id] = true
  if why == nil then
    console.print('home: ' .. id .. ' has no picture')
  else
    console.print('home: ' .. id .. ' has no picture: ' .. tostring(why and why.message or why))
  end
end

-- **The picture of a cell, painted again every time the cell asks for it** (0.178.0): the letter of the app on paper and,
-- over it, the file the manifest names. It is one function because the cell can be **on ink** —the dial is on it— and then
-- the whole picture is turned over at the end: the paper of the icon becomes the black of the cell and the greys of the
-- drawing stay, which is what the old launcher did with one call over its selected cell and the reason its icons looked
-- as if they had no box (docs/30, docs/92).
local function paint_picture(id, picture, info, inverted)
  -- **A picture that is already in the block is a copy and not a decode** (0.179.0): the same pixels the file and the
  -- decoder made the first time, and no disk at all. It is always the paper face —the ink is the `invert` at the end— so
  -- the cache holds one picture per app and not two (docs/30, docs/75).
  local ours = the_cache_is_ours()
  local record = ours and cached[id] or nil
  if record ~= nil and warm[id] and cache:size() >= kCachePictures + (record.slot + 1) * picture_bytes() then
    local at = kCachePictures + record.slot * picture_bytes()
    if picture:blit(cache, at, 0, 0, kIcon, kIcon, kCellMode) then
      if inverted then picture:invert(0, 0, kIcon, kIcon, kCellMode) end
      return
    end
    warm[id] = nil
  end

  picture:clear(15, kCellMode)

  local letter = string.upper(string.sub(id, 1, 1))
  if letter ~= '' then
    local scale = 4
    local wide = ui.text_width(letter, scale)
    local high = ui.line_height(scale)
    picture:text((kIcon - wide) // 2, (kIcon - high) // 2, letter, scale, 0, kCellMode)
  end

  if info ~= nil then
    if info.icon == '' then
      say_the_fault(id, nil)
    else
      local drawn, why = picture:image(0, 0, info.icon, kIcon, kIcon, kCellMode)
      if not drawn then say_the_fault(id, why) end
    end
  end

  -- **And what was just drawn is kept**: the paper face only, because the ink is the invert of the same pixels, and only
  -- when the block has a place for it —an app whose manifest has not arrived has no record yet, and a block of a board with a
  -- crowd of apps may hold the records and no picture at all— (docs/30, docs/75).
  if record ~= nil and not inverted and info ~= nil and the_cache_is_ours() and
      cache:size() >= kCachePictures + (record.slot + 1) * picture_bytes() then
    local at = kCachePictures + record.slot * picture_bytes()
    if picture:capture(cache, at) ~= nil then warm[id] = true end
  end

  if inverted then picture:invert(0, 0, kIcon, kIcon, kCellMode) end
end


-- One cell of the page: the picture, the name and the press. `known[id]` is the manifest when it has arrived and `nil`
-- while the queue has not asked for it, and the same code draws both, so a cell that gains a name is not a second page.
local function add_cell(cell_w, cell_h, name_w, id)
  local info = known[id]
  local cell = ui.grid_entry(cell_w, cell_h)
  if cell == nil then return end

  cell.background = 15

  -- **A picture with no room says so** (0.174.5): the cell shows the letter and the console says why, because a grid
  -- of names with four icons missing and no line about it is a page that hides what the memory answered (`ui.canvas`
  -- gives `nil, error` and not a box that is not there, docs/30).
  local picture, no_room = ui.canvas(kIcon, kIcon)
  if picture ~= nil then
    picture:place((cell_w - kIcon) // 2, 4)
    -- **The picture paints itself, and it hears whether it is on ink** (0.178.0): the cell asks its children to draw again
    -- when it turns over, and the second argument is the face it leaves behind. A canvas of the app with no `on_draw`
    -- cannot turn over —it would compose the same white box on the block— and that is all this callback is for
    -- (docs/30, docs/92).
    picture.on_draw = function(self, inverted)
      paint_picture(id, self, known[id], inverted)
    end
    -- **And nothing is painted here**: a canvas that was just made owes a draw, so the system asks it —with `false`, the
    -- paper face— and a second paint of the same pixels would be the page paying twice for one picture (docs/30). It is
    -- also the moment the picture is kept in the cache of the session, which is why this file does not draw it here.
    cell:add(picture)
  else
    console.print('home: ' .. id .. ' has no picture: ' .. tostring(no_room and no_room.message or no_room))
  end

  local name = ui.label(name_w, kNameLine)
  if name ~= nil then
    name.text = (info ~= nil) and info.title or id
    name.scale = kName
    name.align = 'center'
    name.color = 0
    name.background = 15
    name:place(kPad, kIcon + 6)
    cell:add(name)
  end

  -- The finger and the centre of the dial both come here: the click of a cell is the click of a control (docs/30).
  cell.on_click = function()
    -- **The use is counted before the app is asked for** (0.189.13): a launch that fails is still a launch the person made,
    -- and the count is what the page sorts by when `cfg sort` is on.
    bump_usage(id)
    local ok, err = app.run(id)
    if ok then
      console.print('home: ' .. id)
    else
      console.print('home: ' .. id .. ' cannot start: ' .. tostring(err and err.message or err))
    end
  end

  -- **The page follows the dial, and the grid writes the cursor itself** (0.179.4): a cell that takes the ring is the page
  -- the control turns to —its footer says `N / M` and the cells of the other pages are out of its frame—, so this file has
  -- no `on_focus` and no paginator of its own: the number `index` used to be written here, one line per cell, and the
  -- system does it now in the one place where the focus arrives (docs/30, docs/90).
  grid:add(cell)
  cells[id] = { cell = cell, picture = picture, name = name }
end

-- **The size of the cell, which is the key of the cache** (0.179.0): the block keeps pictures of a side, and a page that
-- has been resized has a different one —so the side is measured once, before the cache is read, and a block of another side
-- is not painted on this page—. It is the arithmetic `icon_grid_cells` does and not this file's (docs/30, docs/90).
local function measure()
  local width = view.width - 2 * kMargin
  kIcon = (width // kColumns) - 20
  if kIcon > 72 then kIcon = 72 end
  if kIcon < 24 then kIcon = 24 end
  kCellH = kIcon + 8 + kNameLine + 8
end

local function build()
  give_back()

  local width = view.width - 2 * kMargin
  local top = kTop
  local height = view.height - top - kMargin
  if width < 200 or height < 120 then
    console.print('home: the glass is too small for a grid in it')
    return
  end

  -- **The mark of a cell is as wide as the cell allows and no more than the system draws** (0.166.1): the same
  -- arithmetic `icon_grid_cells` does, with the padding of ten and the ceiling of a hundred and twenty, so the picture
  -- the app makes is the box the grid has for it (docs/30, docs/90).
  measure()

  -- **The page is the app's whole and not the box of its controls** (0.153.1, docs/40): a control paints inside its own
  -- frame, so the air around the grid keeps whatever the glass had —the picture of the boot— if this page does not wash
  -- it. The wash is also what makes the first present a whole page and not a box of it (docs/50).
  ui.clear(15)

  grid = ui.icon_grid(width, height)
  if grid == nil then
    console.print('home: there is no room for the grid of the apps')
    return
  end

  grid.columns = kColumns
  grid.scale = kName
  -- **The cell the dial is on, which is also the page the grid is on**: the first one until somebody walks (0.157.0).
  grid.index = 1
  grid:place(kMargin, top)

  local cell_w = width // kColumns
  local name_w = cell_w - 2 * kPad

  -- **Every name is a cell of the grid and the pages are the control's** (0.157.0): the cells that do not fit its frame
  -- are placed out of sight by the grid and come back when the dial reaches them, and its foot says the page it is
  -- (`icon_grid_cells`, docs/90). A launcher that handed it one page at a time was a launcher that owned a paginator.
  for i = 1, #ids do
    add_cell(cell_w, kCellH, name_w, ids[i])
  end

  -- **The ring is on the first cell** (0.153.0, docs/30): a page that left the dial outside would answer «nothing» to the
  -- first press of the middle key, which is the one a person makes. **The cells are the stops and the grid is not**
  -- (0.156.1): a grid that holds the app's cells is a box, and the walk of the dial goes from the last cell to the first
  -- one round the controls that are on the glass.
  if ids[1] ~= nil and cells[ids[1]] ~= nil then
    ui.focus(cells[ids[1]].cell)
  end

  -- **What this page can say of itself**: the cells it holds. The page and how many there are is the grid's to say, at
  -- its foot, and the console says how many names the door answered (0.157.0).
  console.print(string.format('home: %d cells', #ids))

  -- **And the strip of the foot says how far the queue is while it runs** (0.189.15): the cells are up with their letters
  -- and the names and the pictures are still arriving one a turn, so the line under the strip of the system —and not the
  -- band of the dots— is the place that says it. **It is as wide as the glass** and not as wide as the grid: a loading bar
  -- that stops twenty pixels short of the edges is a control of a form, and this one is the edge of the page (docs/90).
  show_the_bar(view.width)

  if #ids == 0 then
    -- **An empty grid says why it is empty** instead of showing nothing: a disk with no app and a page that failed look
    -- the same on the glass, and only one of them is a fault (docs/45).
    local empty = ui.label(width, kNameLine * 2)
    if empty ~= nil then
      empty.text = 'No app on this disk runs on this board'
      empty.scale = kName
      empty.align = 'center'
      empty.color = 0
      empty.background = 15
      empty:place(0, 0)
      grid:add(empty)
    end
  end

  -- **The quality of the page is the system's** (0.179.5): the launcher hands over a page of words and one-colour pictures
  -- and the kernel knows what the glass had —it washed it before this app drew— so the pass that flashes is not this file's
  -- to ask for (docs/30, docs/50).
  ui.present()
end

-- ------------------------------------------------------------------ the queue

-- **One app of the queue per turn** (0.154.0): the manifest of the next name, and the cell it belongs to learns the name
-- a person reads and the picture to press. An app that turns out to be a launcher of its own, or one of another board,
-- leaves the page and the page is laid out again without it when the queue is over; one whose manifest cannot be read
-- keeps its name and its letter and says why, which is the only thing this page can honestly do.
local function step()
  local id = queue[head]
  head = head + 1
  if id == nil then return end

  local info = apps.info(id)
  if info == nil then
    console.print('home: ' .. id .. ' has no manifest this system reads')
    return
  end

  if info.launcher or not info.works_here then
    if info.launcher then
      console.print('home: ' .. id .. ' is a launcher')
    else
      console.print('home: ' .. id .. ' needs another board')
    end
    again = true
    for i = #ids, 1, -1 do
      if ids[i] == id then table.remove(ids, i) end
    end
    return
  end

  known[id] = info
  -- **The record of this app in the block, with a place of its own for its picture**: the paint that follows is the one
  -- that reads the file and keeps the pixels, and it needs the number before it draws (0.179.0, docs/75).
  if cached[id] == nil then
    cached[id] = { title = info.title, icon = info.icon, slot = slots }
    slots = slots + 1
  end
  console.print(string.format('home: app %s %s', id, info.title))
  local done = cells[id]
  if done ~= nil then
    if done.name ~= nil then done.name.text = info.title end
    if done.picture ~= nil then
      -- **And the picture paints again, by itself**: its `on_draw` is what knows how to put the file over the letter and
      -- how to turn the whole box over when the dial is on the cell (0.178.0, docs/30).
      done.picture:invalidate()
    end
  end
end

-- ------------------------------------------------------------------ the version of what is kept

-- **Una versió de menys és una cel·la que menteix** (0.189.22, l'amo: *«Si tens una app en una versió anterior has de
-- descartar l'entrada d'aquesta app del caché de la sessió \"apps\" per refrescar l'icona i el numero de versió»*): the
-- record of the block carries the version it was read from, and the disk is asked one app per turn —the same rhythm as the
-- queue of manifests, and the page is already on the glass— whether that number is still the one the app says. When it is
-- not, the record takes the new title, the new path and the new version and **gives its picture back** (`warm[id] = nil`):
-- the cell draws the letter and the icon of the disk again and stores the new picture in the same place of the block, so
-- nothing of the old version survives (docs/30, docs/40, docs/75).
-- **El que la comprovació d'un torn ha trobat**: `nil` quan no hi havia res a mirar, `'changed'` quan la cel·la s'ha
-- tornat a pintar amb la versió nova, i **`'gone'` quan el disc diu que l'app ja no és d'aquesta pàgina** —un llançador o una
-- app d'una altra placa—, que és el cas que la caché no pot veure i que abans queia a la graella per sempre (0.189.28).
local function check_the_version_of(id)
  local record = cached[id]
  if record == nil then return nil end
  local info = apps.info(id)
  if info == nil then return nil end            -- un manifest que no es pot llegir avui no esborra el que hi havia
  -- **La porta té l'última paraula sobre si l'app és d'aquesta pàgina** (0.189.28): a cache says the row was drawn here,
  -- and a manifest that has changed since —a board list, a `launcher`— is the one thing the block cannot know. The record is
  -- forgotten and the page is laid out again, which is where the door's list and the page meet (docs/30, docs/40).
  if info.launcher or not info.works_here then
    console.print(string.format('home: %s is not of this page any more (%s)', id,
                                info.launcher and 'a launcher' or 'it needs another board'))
    cached[id] = nil
    warm[id] = nil
    known[id] = nil
    return 'gone'
  end
  if info.version == record.version then return nil end
  console.print(string.format('home: %s is %s (was %s), the picture is read again', id,
                              (info.version ~= '') and info.version or 'a version with no number',
                              (record.version ~= '') and record.version or 'no version'))
  record.title = info.title
  record.icon = info.icon
  record.version = info.version
  -- **El número del seu dibuix no es toca**: the picture of the new version is written over the old one and the block does
  -- not grow a hole for an app that is still the same app (docs/75).
  warm[id] = nil
  known[id] = info
  local cell = cells[id]
  if cell ~= nil then
    if cell.name ~= nil then cell.name.text = info.title end
    if cell.picture ~= nil then cell.picture:invalidate() end
  end
  return 'changed'
end

-- ------------------------------------------------------------------ the app

-- **The names of the door and nothing read yet**: this is what makes the page arrive at once, and it is the only call of
-- the door a page needs. This app is not in it —a launcher does not list itself— and the rest of what a cell says is asked
-- for one app per turn from the loop below, unless the cache of the session already knows it.
local function ask_the_door()
  ids = {}
  local all = apps.list()
  if all == nil then
    console.print('home: the door of the apps did not answer')
    return false
  end
  for i = 1, #all do
    if all[i] ~= app.id then
      ids[#ids + 1] = all[i]
    end
  end
  -- **The page is in the order of the names of the door and it does not move after** (0.154.0): the titles arrive one at a
  -- time, and a grid that reorders itself when one lands is a grid whose cells jump under the finger. The order of the
  -- disk is not one a person can learn either, so the names are sorted once, here, before the first cell. **And the person
  -- may have asked for another order** (0.189.13): with `cfg sort on` the apps they open most come first, and the name is
  -- the tie (docs/40).
  table.sort(ids)
  by_usage(ids)
  console.print(string.format('home: %d ids: %s', #ids, table.concat(ids, ' ')))
  return true
end

-- **What a page of these names needs from the block and from the queue**: a name the cache has is a cell that is already
-- whole —title and picture— and one it has not is a turn of the queue (docs/30, docs/75).
local function fill_from(names)
  for i = 1, #names do
    local id = names[i]
    local was = cached[id]
    if was ~= nil then
      -- **El registre del bloc no és un manifest, i el que la pàgina en sap s'hi posa** (0.189.28, i va ser una regressió meva de
      -- la 0.189.22): `reconcile` pregunta `launcher` i `works_here` a cada fila, i el que ve del bloc només porta títol, icona i
      -- versió. Amb els dos camps absents, **totes** les apps de la pàgina que venia del bloc es llegien com «needs another board»
      -- i la graella quedava a zero cel·les (l'amo ho va veure al log de la placa: 12 ids, 12 «needs another board», 0 cells).
      -- I no és una mentida: **una fila del bloc és una fila que aquesta pàgina va dibuixar**, i aquesta pàgina només dibuixa el
      -- que corre aquí; el que pot haver canviat és el manifest, i d'això se n'encarrega `check_the_version_of`, que ara també té
      -- la paraula sobre si l'app encara és d'aquesta pàgina (docs/30, docs/40).
      known[id] = { title = was.title, icon = was.icon, version = was.version or '', works_here = true, launcher = false }
    else
      queue[#queue + 1] = id
    end
  end
end

-- **The door is asked again while the page is up, once** (0.179.1): the page of a warm cache is built from the block
-- before anything is read —it is what makes the return instant— and the door answers in the turn after. When its list is the
-- one the page has, nothing happens at all; when an app was installed or removed in this session, the page is laid out
-- again with the door's list, which is the price of not showing a page that lies (docs/30, docs/40).
--
-- **I el que la pàgina no dibuixaria no és un canvi** (0.189.19): the door lists the app that needs another board and the
-- app that is another launcher, and this page draws neither of them. Discovering it one turn later in `step` set `again`,
-- and `again` is a whole `build()` —another paint of the grid for a cell that was never there— at **every** start, because
-- the cache keeps only what the page drew and the door always answers one more (the owner, 14/11: *«perqué home_eink
-- refresca 3 vegades la seva vista recent obert?»*: it was the page of the block, the page of the door's list and the page
-- without the app that does not run here). The list is filtered here, with **one manifest read for each id the page does
-- not know yet**, which is exactly what the queue of `step` would have paid one turn at a time. A page whose list is the
-- same after the filter is a page that is already on the glass (docs/30, docs/40).
local function reconcile()
  local before_count = #ids
  local before_first = ids[1]
  local before_last = ids[#ids]
  if not ask_the_door() then return false end

  -- **Les que no són d'aquesta pàgina es queden fora abans de comparar**, i el manifest que s'ha llegit queda a `known`
  -- perquè la cua no el torni a demanar.
  local keep = {}
  for i = 1, #ids do
    local id = ids[i]
    local info = known[id]
    if info == nil then info = apps.info(id) end
    if info == nil then
      console.print('home: ' .. id .. ' has no manifest this system reads')
    elseif info.launcher then
      console.print('home: ' .. id .. ' is a launcher')
    elseif not info.works_here then
      console.print('home: ' .. id .. ' needs another board')
    else
      keep[#keep + 1] = id
      known[id] = info
    end
  end
  ids = keep
  table.sort(ids)
  by_usage(ids)

  if #ids == before_count and ids[1] == before_first and ids[#ids] == before_last then
    local same = true
    for i = 1, #ids do
      if known[ids[i]] == nil then same = false end
    end
    if same then return false end
  end
  fill_from(ids)
  return true
end

-- **The cache of the session, before the first cell** (0.179.0): the side of a picture is what names its place in the block,
-- so the cell is measured first. A block with records is a page that is built at once; a cold one —or a disk whose names
-- the block does not have— asks the door first, which is what this launcher did before there was a cache (docs/30, docs/75).
measure()
load_cache()
-- **The word of the person about the order, and what this page has learned about them** (0.189.13): `sys.settings()` is
-- read once, because `cfg sort` is a setting and not something this page changes while it is up, and a system that keeps
-- no settings answers `nil, error` —the page is then the door's order, which is what it was before the switch existed—
-- (docs/30, docs/40). The count is read before the first cell so that the page that comes out of the block is already in
-- the order the person chose, and the door's answer in the turn after finds it and does not lay the page out again.
local settings = sys.settings()
if settings ~= nil then sort_by_usage = settings.sort_by_usage == true end
load_usage()
local from_cache = false
if #cached_order > 0 then
  for i = 1, #cached_order do
    if cached_order[i] ~= app.id then
      ids[#ids + 1] = cached_order[i]
      from_cache = true
    end
  end
end
if from_cache then
  by_usage(ids)
  fill_from(ids)
  -- **I el que ha vingut del bloc es comprova, una app per torn**: the version of each record against the version the disk
  -- says, because a page that comes back whole can still be a page of yesterday's apps (0.189.22, docs/30).
  for i = 1, #ids do
    if cached[ids[i]] ~= nil then verify[#verify + 1] = ids[i] end
  end
else
  ask_the_door()
  fill_from(ids)
end

-- **The size of the page that is on the glass** (0.168.3): a call of `on_resize` that says the size the page already has is
-- not another view, and building again for it would throw away the canvases of the cells that are already on the glass —
-- seven pictures of 72 x 72 at four bits are 18 144 B and a whole-page paint, for nothing—. **What this guard does not fix,
-- and it is written here so nobody believes it does**: the lost pictures of a launcher. Those were measured in a run where
-- the app was started **twice** —`lunokOS` boots its home app and the emulator asked for it again with `--run home_eink`— and
-- the log of it says `[I][APPS] home_eink` twice; started once, the page is built once (measured, 8/11) (docs/30, docs/50).
local page_w = 0
local page_h = 0

build()
page_w = view.width
page_h = view.height

-- **The system gave the app another view** (0.153.0, docs/30): the whole glass when the person asks for it from the bar
-- and the glass under the bar when three taps give it back. The page is built again in the new box, with the manifests
-- that have already arrived, and the queue goes on where it was. **And a call that says the size the page already has is
-- not another view**: rebuilding for it throws away the canvases of the cells that are already on the glass (0.168.3).
function on_resize(width, height)
  if width == page_w and height == page_h then return end
  page_w = width
  page_h = height
  view = { width = width, height = height }
  -- **A page of another size has cells of another size**, and the pictures in the block were measured for the old one: they
  -- are not painted on this page, and the ones this run draws are kept in their places (the magic is rewritten with the new
  -- side, so a block of two sides is a block of none and is built again at the next start) — docs/30, docs/75.
  local was = kIcon
  measure()
  if kIcon ~= was then
    warm = {}
    cached = {}
    slots = 0
    if the_cache_is_ours() then cache:write(0, '    ') end
  end
  build()
end

-- **The loop is the queue and the paint**: a turn asks for the manifest of one app and shows what changed; a turn with
-- nothing to ask only keeps the kernel turning, and a present with nothing new draws nothing (docs/30, docs/90).
local kept = false
-- **The door is asked in the turn after a page that came from the block** (0.179.1): a page of the cache is on the glass
-- before anything of the disk is read, and the turn that follows is the one that says whether it is still the disk's list.
local checked = not from_cache
-- **I un avís de la porta torna a fer les dues coses** (0.189.23): `on_event` is the knock the system gives the launcher
-- that `cfg home` names when an app is installed or removed (`repo install`, `apps remove`), and all it does here is drop
-- these two marks —the knocks never draw, because a hook runs between two turns and the page is drawn in its own turn—.
local apps_changed = false

-- **El sistema diu que la llista d'apps ha canviat** (0.189.23, docs/30, docs/40): `ev.kind` is what the door is talking
-- about and `ev.what` the verb (`install` or `remove`). This page does not need to know which app: it asks the door again
-- (`reconcile`), which is the one thing that tells it what the grid has to have — the same path a page of yesterday takes
-- when the disk is another one—. **The marks are dropped and not answered here**: a hook cannot await and a page drawn from
-- inside one would draw between turns, so the turn after this one is the one that reconciles, adds or removes the cells that
-- changed and paints only those (docs/30, docs/50).
function on_event(ev)
  if type(ev) ~= 'table' or ev.kind ~= 'apps' then return end
  console.print(string.format('home: the list changed (%s), generation %d', tostring(ev.what),
                              tonumber(ev.generation) or 0))
  apps_changed = true
end

while true do
  local painted = false
  if lost then
    -- **The block that was taken is asked for again here and not inside a draw** (0.179.8): the steps of the cache are the
    -- same ones, so a board that is still short lands on a block of the names —or on none, and the page goes on without it—
    -- and the cells of this page keep the names they already have (docs/30, docs/75).
    lost = false
    load_cache()
    kept = false
  end
  -- **L'avís del sistema s'atén al torn i no dins del ganxo** (0.189.23): `checked` fa que la porta es torni a preguntar
  -- —`reconcile` afegeix o treu les cel·les que han canviat i no toca les altres— i `kept` fa que el bloc es torni a escriure
  -- al final de la cua, o sigui que l'app que ha arribat es queda a la caché i la que ha marxat en surt (docs/30, docs/75).
  if apps_changed then
    apps_changed = false
    checked = false
    kept = false
  end
  if head <= #queue then
    step()
    -- **The bar moves with the turn it has just taken**: one app of the queue is one step of the strip, and the cell it
    -- filled is painted in the same present (0.186.2).
    if bar ~= nil then bar.value = queue_percent() end
    painted = true
  elseif again then
    -- **The page is laid out again once**, and only when an app left it: a grid whose cells are laid out for a name that
    -- is not an app of this page any more is a grid with a hole in it (0.154.0).
    again = false
    build()
    painted = true
  elseif not checked then
    checked = true
    if reconcile() then
      build()
      painted = true
    end
  elseif vhead <= #verify then
    -- **Una versió per torn** (0.189.22): the check runs after the list of the door and the page are the ones of this run, and
    -- the turn only paints when a cell changed (the picture of the new version and its name). The cache is written after it,
    -- so what is kept is the number that the disk has today (docs/30, docs/40).
    -- **I una app que ja no és d'aquesta pàgina fa que la pàgina es torni a fer** (0.189.28): el bloc la tenia dibuixada i el
    -- disc diu que no hi va, i qui decideix què hi ha a la graella és la llista de la porta i no el que hi havia.
    local seen = check_the_version_of(verify[vhead])
    vhead = vhead + 1
    if seen == 'changed' then
      painted = true
    elseif seen == 'gone' then
      checked = false
      painted = true
    end
  elseif head > #queue and not kept then
    -- **And the cache is written once the page is whole** (0.179.0): every manifest has arrived and every picture it could
    -- draw is in the block, so the next run of this launcher —which is every time an app ends— has no disk to read
    -- (docs/30, docs/75). **The line under the strip goes in this same turn** (0.189.15): the queue is over, so the top of
    -- the page is paper again and the dots of the paginator are where they always were, at the foot of the grid.
    kept = true
    finish_the_bar()
    painted = true
    save_cache()
  end

  -- **The page goes up as the cells come** (0.166.1): one present per cell, right after its picture, because a single
  -- present of the whole page when the queue was over was tried and measured wrong —it lands before the cells arrive, so
  -- the last pictures of the grid stay unwritten— and what the owner asked for is that the cell the system has just
  -- filled is the one that goes to the glass (docs/30, docs/50).
  if painted then
    -- **A cell that has just arrived is one box and not the page** (0.179.5): a picture of every cell is a `gc16` of the
    -- whole grid —ten flashes for ten cells— and a cell is what changed, so the system paints the box the cell left and
    -- cleans it without the flash (docs/50).
    ui.present()
  end
  sys.delay(50):await()
end
