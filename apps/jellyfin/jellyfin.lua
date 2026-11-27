-- ============================================================================
--  jellyfin: **el comandament d'un servidor Jellyfin de la xarxa local**
--  (portat de `PaperOS :: apps/Jellyfin`, versió 2.10.0).
-- ============================================================================
--  THE REMOTE OF A JELLYFIN SERVER: who is connected, what is playing, the four
--  commands a remote has (play/pause, previous, next, stop), a bar that moves the
--  film to a position, a library refresh, and the list of what is half watched.
--
--  WHAT IT IS NOT, AND WHY: it is not a player. This board has no speaker and no
--  decoder, so the app talks to a server on the LAN over its plain http port and
--  orders the clients that ARE playing. The address and the API key are the two
--  things it needs, and they are typed once in the settings (the gear).
--
--  WHERE THE SETTINGS LIVE: `/data/jellyfin/settings.txt` —`server_url=` and
--  `api_key=`, one per line. The file is plain text: anybody who can read the
--  disk can command the server, and that is said in `Jellyfin.md` and not
--  discovered later.
--
--  HOW TO MOVE AROUND: the four screens are four TABS of the system's strip (l'amo
--  del paper, 20/10: *«no usis botons usa tabs»*), and the buttons left on each
--  page are its ACTIONS and nothing else. The settings are not a tab: they are the
--  gear at the top right (21/10: *«Treu settings dels tabs, usa un botó amb una
--  icona de roda dentada a dalt a la dreta sobre les tabs»*).
--
--  WHAT CHANGED IN THE PORT, said here and not discovered later:
--    - the page is built with the controls of this system and not painted by hand
--      —there is no `on_draw`, `on_tap` nor `on_gesture` here (docs/45, trap 7)—:
--      the remote of the paper is a page of keys with the same seven of them and
--      the sizes the owner asked for (24/10: *«Els botons? una mica mes grans»*);
--    - the pictures are the server's PNGs drawn with `ui.image`/`cv:image`, and the
--      `.cache.ics` of the paper does not travel: here the cache of a picture is
--      the system's `fs.thumbnail`, and it caps at 256 px (docs/30, docs/46);
--    - **the orders are POST on the server and this system only speaks GET**
--      (`http.get` and `http.download`): the same routes go out as a GET and a real
--      server answers **405**, which is what the line of state says. It is a hole
--      of the system and not of the app (see `api.lua`, `M.command()`).
-- ============================================================================

local ok_json, json = pcall(require, 'json')
if not ok_json then json = nil end
local ok_api, api = pcall(require, 'api')
if not ok_api then api = nil end

local function say(text)
  console.print('jellyfin: ' .. tostring(text or ''))
end

-- ---------------------------------------------------------------- the numbers of the page

-- A page needs a glass: a board of lights lists this app and running it says so and ends (docs/30, docs/40).
if not sys.caps().display then
  say('this board has no glass')
  return
end

local view = { width = 0, height = 0 }
do
  local c = ui.caps()
  view.width, view.height = c.width, c.height
end

-- The sizes of the paper project in pixels of line, asked of the face the system draws with: the same page at the same
-- physical size on both boards (docs/45 § 3).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end
  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end
  return scale
end

local kSmall = scale_for(33)
local kBody  = scale_for(43)
local kTitle = scale_for(57)

local Margin = 12
local GearW, GearH = 56, 48   -- the floor of a finger this house gives everything a person presses
local TabH = 63               -- 24/10, l'amo: *«Les tabs en mode vertical son massa estrets, no es poden pitjar bé?»*
local Gap = 8

local INK, GREY, PAPER = 0, 8, 15

-- The picture of the control page is the band of the glass (25/10, l'amo: *«Fes la imatge de portada del tamany de l'ample
-- de la pantalla»*) and the grid keeps the poster of 200 x 300, which is what a cell holds.
local ArtW, ArtH = 540, 360
local PosterW, PosterH = 200, 300

-- The cards of what is half watched: 512 x 336 and two per page (24/10).
local CardH = 336
local CardPad = 10
local ColGap = 16
local CardBarH = 14

-- The keys of the control page (25/10, l'amo: *«Els botons? una mica mes grans»*).
local CtlBig, CtlSmall = 96, 68

-- The clock of the page: every PollMs the list is asked again QUIETLY so that the position on the glass is set back to the
-- server's word (24/10, l'amo: *«Fes que a "Control" el temps corri segons si el video está en play o pausa. De tant en tant
-- connecta per rectificar a quina part del vídeo estem»*).
local PollMs = 30000

-- ---------------------------------------------------------------- the state of the app

local Pages  = { 'main', 'sessions', 'continue', 'control' }
local Titles = { 'Server', 'Sessions', 'Continue', 'Control' }

local screen_   = 'main'
local url_      = ''
local token_    = ''
local status_   = ''
local sessions_ = {}
local detail_   = {}     -- the detail of every session of the last answer, by the index of the row
local chosen_   = 0
local saved_chosen_ = ''
local cut_      = false
local info_name_, info_version_ = '', ''
local counts_   = nil
local user_     = ''

-- What is playing in the session that is chosen: the identity, the way, where it is and how long it is. Seconds and not
-- ticks, because a tick count does not fit in an integer of this engine and `api.seek()` appends the seven zeros as text.
local line_, way_ = '', ''
local at_, len_, can_ = 0, 0, false
local playing_, at_ms_ = false, 0
local mute_, can_mute_ = false, false
local item_id_, poster_of_ = '', ''

-- What is half watched, and the page of it: eight rows at most, like the paper (a saved table of the system holds eight
-- keys per row and the block does not grow).
local resume_, picked_, resume_page_ = {}, 0, 1
local asking_resume_ = false

-- One question of the list at a time (the answer of the second would be read in the place of the first), and whether the
-- question is the clock's own, which says nothing on the glass.
local asking_, quiet_ask_ = false, false

-- The page the gear was pressed on, so that the gear takes a person back there and not to the first page.
local gear_from_ = 'main'

-- The controls and the shape of the view. They are rebuilt by `show()` and are never part of the state.
local owned, named = {}, {}
local shape = { focus = nil }

-- Every function that a page's handler or another question calls is declared here, before any of them is written: a `local`
-- declared after the function that reads it is not that local at all, it is a GLOBAL that is `nil` (trap 31 of
-- `Jellyfin.md`, 24/10).
local show, ask_info, ask_counts, ask_sessions, ask_resume
local refresh_main, refresh_control, load_card, ask_poster, save_state, save_settings

-- ---------------------------------------------------------------- small helpers

local function keep(control)
  if control ~= nil then owned[#owned + 1] = control end
  return control
end

local function give_back()
  for i = #owned, 1, -1 do
    pcall(function() owned[i]:release() end)
  end
  owned, named = {}, {}
end

--[[
**UN GANXO NO POT ESPERAR** (0.198.2 del temps, i docs/30): `:await()` dins d'un `on_click`, d'un `on_change` o d'un `on_page`
respon **`E_NOTASK`**, perquè el ganxo es crida amb la coroutine de l'app aturada. O sigui que el que fa un ganxo és dir què
vol —`want()`— i el bucle, que sí que espera, és qui ho fa: el mateix pacte que el botó de `Save` del temps (l'amo, amb
l'app a la mà: *«send nothing?»* i la línia `E_NOTASK` al registre de la placa).
]]
local wish = nil

local function want(job)
  wish = job
end

local function fit(text, n)
  if api then return api.fit(text, n) end
  text = type(text) == 'string' and text or ''
  if #text <= n then return text end
  return text:sub(1, n)
end

--[[
WHAT THE SCREEN SAYS, AND THE LOG SAYS IT TOO —and with no cut—: a reason cut in the middle is a reason nobody can act on,
and the log has no such ceiling. The owner asked for it with the original app in his hand (20/10): *«Fes que els errors en
pantalla es vegin al log també»*.
]]
-- **Un text que canvia no arriba al vidre sol**: `ui.present` és l'única porta (docs/30), i una línia d'estat que es
-- queda a la memòria és una app que no diu res. El pass s'omet mentre la pàgina es construeix, que és quan els controls que
-- canvien el text encara no tenen lloc.
local page_ready_ = false

local function repaint()
  if page_ready_ then ui.present('gc16') end
end

local function set_status(text)
  local said = tostring(text or '')
  status_ = said
  if named.status ~= nil then named.status.text = fit(said, 96) end
  say(said)
  repaint()
end

local function configured()
  return url_ ~= '' and token_ ~= ''
end

-- Without its two modules the app has no addresses and no reader: the page says so and no question leaves.
local function no_modules()
  if json ~= nil and api ~= nil then return false end
  set_status("The app's own modules are missing.")
  return true
end

local function chosen_session()
  return sessions_[chosen_]
end

-- Seconds as a person reads them: "3:07" and, when there is an hour, "2:00:07".
local function clock(secs)
  local n = math.floor((tonumber(secs) or 0) + 0.5)
  if n < 0 then n = 0 end
  local h = math.floor(n / 3600)
  local m = math.floor((n % 3600) / 60)
  local s = n % 60
  if h > 0 then return string.format('%d:%02d:%02d', h, m, s) end
  return string.format('%d:%02d', m, s)
end

-- Where the film is RIGHT NOW: the word of the server plus the time that has gone by while it plays, and only while it
-- plays —a paused session's clock does not move, and neither does this one—.
local function pos_now()
  local at = at_
  if playing_ and at_ms_ > 0 then
    local gone = sys.time_ms() - at_ms_
    if gone > 0 then at = at + gone / 1000 end
  end
  if at < 0 then at = 0 end
  if len_ > 0 and at > len_ then at = len_ end
  return at
end

local function page_at(name)
  for i = 1, #Pages do
    if Pages[i] == name then return i end
  end
  return 0
end

-- ---------------------------------------------------------------- the two files of the app

local function settings_path() return fs.data('settings.txt') end
local function poster_path() return fs.data('poster.png') end

-- A picture of a card, named after the item and short enough for a disk that holds 32 characters per name: the first twelve
-- of an id of thirty-two are as good as all of them and the name is twenty.
local function card_path(id)
  return fs.data('p' .. fit(id, 12) .. '.png')
end

local function read_settings()
  local text = fs.read(settings_path())
  local cfg = {}
  if type(text) == 'string' then
    for line in text:gmatch('[^\r\n]+') do
      local k, v = line:match('^%s*([%w_]+)%s*=%s*(.-)%s*$')
      if k ~= nil then cfg[k:lower()] = v end
    end
  end
  url_, token_ = cfg.server_url or '', fit(cfg.api_key or '', 40)

  -- The address is completed on every start and not only when it is saved: a file with `host:8096` in it has no scheme and
  -- the request would be refused. `api.base()` is the same door the Save button uses, so there is one rule and not two.
  if url_ ~= '' then
    local base, why = api and api.base(url_)
    if base then
      url_ = base
    else
      say('the saved address: ' .. tostring(why))
    end
  end
end

local function write_settings()
  if fs.write(settings_path(), string.format('server_url=%s\napi_key=%s\n', url_, token_)) == nil then
    set_status('The settings were not saved.')
    return false
  end
  return true
end

--[[
**UNA ORDRE ÉS UN POST** (0.199.3, i el `405` que va sortir al registre de la placa: *«jellyfin: This system cannot POST: the
command did not go»*). `http.post` és la porta nova del nucli —un verb amb el cos buit—, i sense ella una mateixa adreça seria un
`GET` que el servidor refusa. Si la porta no hi és (una placa amb un nucli més vell), es diu i no s'envia res: una ordre que no
surt no és una ordre que ha anat bé.
]]
local function send(address)
  if http.post == nil then return false, 'E_UNSUPPORTED', nil end
  local ok, answer = pcall(function() return http.post(address, 20000):await() end)
  if not ok then return false, tostring((type(answer) == 'table') and answer.code or answer), nil end
  local status = (type(answer) == 'table') and (answer.status or 0) or 0
  if status < 200 or status >= 300 then
    return false, string.format('the server said %d', status), status
  end
  return true, nil, status
end

-- ---------------------------------------------------------------- the questions

local function why_not(problem)
  if problem == nil then return 'the system said nothing' end
  return tostring(problem)
end

--[[
ONE ANSWER, AND THE TWO WAYS OF ASKING FOR IT.

A list of sessions is easily longer than the **16 384 B** that `http.get` keeps, so the long way is a `download` to the
app's own ground and a read back of the file; the short way is the plain GET, and it is the fallback when the file cannot be
read. It is the pair the paper had, with the 4 096 B of its queue replaced by what this system gives (docs/30, docs/70).
]]
local function ask(address, to_file)
  if to_file ~= nil then
    local ok, answer = pcall(function() return http.download(address, to_file, 20000):await() end)
    if not ok then return nil, tostring((type(answer) == 'table') and answer.code or answer), nil end
    local status = (type(answer) == 'table') and (answer.status or 0) or 0
    if status ~= 200 then return nil, string.format('the server said %d', status), status end
    local text = fs.read(to_file)
    if type(text) ~= 'string' or text == '' then return nil, 'the answer was saved and not read', status end
    return text, nil, status
  end

  local ok, answer = pcall(function() return http.get(address, 16384, 20000):await() end)
  if not ok then return nil, tostring((type(answer) == 'table') and answer.code or answer), nil end
  local status = (type(answer) == 'table') and (answer.status or 0) or 0
  if status ~= 200 then return nil, string.format('the server said %d', status), status end
  return (type(answer) == 'table') and (answer.body or '') or '', nil, status
end

ask_info = function()
  if no_modules() then return end
  if not configured() then
    set_status('No server: tap the gear.')
    if screen_ ~= 'main' then show('main') end
    return
  end

  -- THE PUBLIC DOOR: `/System/Info/Public` is the name and the version in a few hundred bytes (`/System/Info` carries the
  -- changelog of every plugin). The key is proved by `/Items/Counts`, which answers 401 to a key that is not good.
  local body, problem = ask(api.url(url_, '/System/Info/Public', token_))
  if body == nil then
    set_status('No answer: ' .. why_not(problem))
    return
  end

  local info = api.info(json, body)
  if not info then
    set_status("The server's answer cannot be read.")
    return
  end

  info_name_, info_version_ = fit(info.name, 24), fit(info.version, 15)
  set_status('Asking the library...')
  refresh_main()
  ask_counts()
end

ask_counts = function()
  if no_modules() then return end
  if not configured() then return end
  local body, problem = ask(api.url(url_, '/Items/Counts', token_))
  if body == nil then
    refresh_main()
    set_status('No counts: ' .. why_not(problem))
    return
  end
  local c = api.counts(json, body)
  if c then counts_ = c end
  refresh_main()
  set_status('Connected.')
end

ask_sessions = function(quiet)
  if no_modules() then return false end
  if not configured() then
    if quiet then return false end
    set_status('No server: tap the gear.')
    show('settings')
    return false
  end

  if asking_ then
    if quiet then return false end
    set_status('The list is already being asked for...')
    return false
  end

  asking_ = true
  quiet_ask_ = quiet == true

  local address = api.url(url_, '/Sessions', token_)
  local body, problem, status = ask(address, fs.data('sessions.json'))

  if body == nil then
    -- The file failed and the GET is the second door: 16 384 B is the whole of the first session, the one that is playing.
    say('the long way gave nothing (' .. why_not(problem) .. '): asking with a GET')
    body, problem, status = ask(address)
  end

  asking_ = false
  local was_quiet = quiet_ask_
  quiet_ask_ = false

  local function said(text)
    if not was_quiet then set_status(text) end
  end

  if body == nil then
    if status == 401 or status == 403 then
      said(string.format('The key was refused (HTTP %d).', status))
    else
      said('No answer: ' .. why_not(problem))
    end
    return false
  end

  local found, complete, detail = api.sessions(json, body)

  if not found then
    -- WHY, IN THE LINE AND IN THE LOG: "the list cannot be read" was every failure at once, and the first bytes are what
    -- tells a wrong address (`<html`) from a body that was cut (docs/45 § 5).
    say(string.format('the list (%d B): %s, first bytes: %s', #body, tostring(complete), fit(body, 48)))
    said('[' .. fit(body, 14) .. '] ' .. tostring(complete))
    return false
  end

  -- THE SESSION THAT WAS CHOSEN SURVIVES THE RE-READ BY ITS ID and not by its place: a client that leaves moves every row
  -- below it, and a person commanding "anna on TV" must not end up commanding the phone because it moved up one line.
  local keep_id = ''
  if chosen_session() then keep_id = sessions_[chosen_].id end
  if keep_id == '' then keep_id = saved_chosen_ end

  sessions_, detail_ = {}, detail
  for i = 1, math.min(#found, 8) do sessions_[i] = found[i] end

  -- WHO THE SERVER THINKS WE ARE, WHICH THE CONTINUE PAGE NEEDS: `/UserItems/Resume` answers 404 without a `userId` and an
  -- API key is nobody, so the only user this app can see is the one a session carries.
  for i = 1, #sessions_ do
    local d = detail_[i]
    if d and (d.uid or '') ~= '' then
      user_ = d.uid
      break
    end
  end

  cut_ = (complete ~= true) or (#found > 8)
  chosen_ = 0
  if keep_id ~= '' then
    for i = 1, #sessions_ do
      if sessions_[i].id == keep_id then chosen_ = i end
    end
  end

  -- ONE SESSION IS NOT A QUESTION (24/10, l'amo: *«Si només hi ha una sessió s'usa aquesta directament»*): in a house there
  -- is one television, and the row that says "choose one" only exists for the day there are two.
  if chosen_ == 0 and #sessions_ == 1 then chosen_ = 1 end

  load_card()

  if screen_ == 'control' then refresh_control() end
  if screen_ == 'sessions' then show('sessions') end

  if cut_ then
    if #sessions_ == 0 then
      said('The list was cut; nothing whole.')
    else
      said(string.format('%d session(s), the list was cut.', #sessions_))
    end
  elseif #sessions_ == 0 then
    said('No active sessions.')
  else
    said('Sessions read.')
  end

  if screen_ == 'continue' and user_ ~= '' then ask_resume() end
  save_state()
  return true
end

--[[
WHAT IS HALF WATCHED, WHICH IS NOT AN ARRAY: `/UserItems/Resume` answers an object with `Items` inside, and `api.resume()`
finds the bracket and hands the rest to the same reader the sessions use (api.lua).
]]
ask_resume = function()
  if no_modules() then return end
  if not configured() then
    set_status('No server: tap the gear.')
    show('settings')
    return
  end

  if user_ == '' then
    set_status('Reading the sessions first...')
    ask_sessions()
    return
  end

  if asking_resume_ then return end
  asking_resume_ = true
  set_status('Reading what is half watched...')

  local address = api.resumeUrl(url_, token_, user_)
  local body, problem, status = ask(address, fs.data('resume.json'))
  if body == nil then body, problem, status = ask(address) end

  asking_resume_ = false

  if body == nil then
    if status == 401 or status == 403 then
      set_status(string.format('The key was refused (HTTP %d).', status))
    else
      set_status('Not asked: ' .. why_not(problem))
    end
    return
  end

  local found, complete = api.resume(json, body)

  if not found then
    say(string.format('what is half watched (%d B), first bytes: %s', #body, fit(body, 48)))
    resume_, picked_ = {}, 0
    set_status('Nothing to continue.')
    if screen_ == 'continue' then show('continue') end
    return
  end

  resume_, picked_ = {}, 0
  for i = 1, math.min(#found, 8) do resume_[i] = found[i] end
  if complete ~= true then say('what is half watched did not end') end

  set_status(#resume_ == 0 and 'Nothing to continue.' or string.format('%d to continue.', #resume_))
  if screen_ == 'continue' then show('continue') end
end

-- ---------------------------------------------------------------- the pictures

--[[
THE PICTURE OF WHAT IS PLAYING, AND THE POSTER BEFORE THE SCENE (24/10).

`Primary` is the poster of a film and it is the picture that says WHAT is playing at a glance; `Backdrop` is the scene of the
film in 16:9, and it is asked only when the library has no poster —the server answers 404 with the reason in the body—. The
band is asked with `fillWidth`/`fillHeight` so that the server CROPS the middle of the artwork to the glass instead of
fitting a narrow poster inside it (`api.poster()`).
]]
ask_poster = function()
  if not configured() then return end
  if item_id_ == '' then
    poster_of_ = ''
    local path = poster_path()
    if fs.stat(path) ~= nil then fs.remove(path) end
    return
  end
  if item_id_ == poster_of_ then return end

  local path = poster_path()
  if fs.stat(path) ~= nil then fs.remove(path) end

  local body = ask(api.poster(url_, token_, item_id_, ArtW, ArtH, 'Primary', true), path)
  if body == nil then
    say('no poster for ' .. item_id_ .. ': asking the Backdrop')
    body = ask(api.poster(url_, token_, item_id_, ArtW, ArtH, 'Backdrop', true), path)
  end

  -- Asked once per item either way: a film without a picture is not a thing that fixes itself by asking twice.
  poster_of_ = item_id_
  if body == nil then say('no picture for ' .. item_id_) end
end

-- The pictures of the two cards of a page, one at a time: a cover that is already on the disk is not downloaded again
-- (24/10, l'amo: *«si esta a caché ja saps que no cal demanar-la»*).
local function fill_pictures(page)
  if not configured() then return end
  local first = (page - 1) * 2
  for i = first + 1, math.min(first + 2, #resume_) do
    local r = resume_[i]
    local path = card_path(r.id)
    if fs.stat(path) == nil then
      if ask(api.poster(url_, token_, r.id, PosterW, PosterH), path) == nil then
        say('no picture for ' .. r.id)
      end
    end
  end
end

-- ---------------------------------------------------------------- the commands

--[[
A COMMAND, WHICH ON THIS SYSTEM IS A GET.

The server's route for a playstate command is `POST /Sessions/{id}/Playing/{name}` and for a general one
`POST /Sessions/{id}/Command/{name}` —a verb with no body—, and this system has no POST (`http.get` and `http.download` are
all of `http`). So the same address goes out as a GET and a real server answers **405**: it is said in the line and in the
log so that nobody looks for the defect in the address (api.lua, `M.command()`).
]]
local function command(name)
  local s = chosen_session()
  if not s then set_status('No session chosen.') return end

  local sent, problem, status = send(api.command(url_, token_, s.id, name))
  if not sent then
    if problem == 'E_UNSUPPORTED' then
      set_status('This system has no http.post: the command did not go.')
    elseif status == 405 then
      set_status('The server does not take this command.')
    else
      set_status('Not sent: ' .. why_not(problem))
    end
    return
  end

  -- THE PAGE ANSWERS BEFORE THE SERVER DOES: a press of the middle key is a clock that starts or stops, and waiting for the
  -- list to come back to say so would make the page lie for a second and a half about the one thing it is made for.
  if name == 'PlayPause' then
    at_      = math.floor(pos_now())
    playing_ = not playing_
    at_ms_   = sys.time_ms()
  elseif name == 'Stop' then
    playing_ = false
  end

  set_status('Sending ' .. name .. '...')
  if screen_ == 'control' then refresh_control() end
  ask_sessions()
end

--[[
THE SILENCE, WHICH IS ANOTHER FAMILY OF COMMANDS: `/Sessions/{id}/Command/ToggleMute` and not `/Playing/...`, two different
enums and two different routes on the server (24/10). The label flips as soon as the order goes out and the next list says
whether the server agrees.
]]
local function toggle_mute()
  local s = chosen_session()
  if not s then set_status('No session chosen.') return end
  if not can_mute_ then set_status('This client does not mute.') return end

  local sent, problem, status = send(api.general(url_, token_, s.id, 'ToggleMute'))
  if not sent then
    if problem == 'E_UNSUPPORTED' then
      set_status('This system has no http.post: the command did not go.')
    elseif status == 405 then
      set_status('The server does not take this command.')
    else
      set_status('Not sent: ' .. why_not(problem))
    end
    return
  end

  mute_ = not mute_
  if screen_ == 'control' then refresh_control() end
  set_status(mute_ and 'Muted.' or 'Sound on.')
end

--[[
THE DECISION OF A SEEK, TAKEN WHERE THE FINGER LETS GO.

The bar of the page is the system's slider: a finger on its tube puts the position where it fell (0.192.34, which is the
paper's draggable bar) and a value reported by the control is the whole decision —this system has no `on_touch` that could
wait for the lift—. The position goes from the length the server gave and never before the beginning or after the end.
]]
local function seek_to(seconds)
  local s = chosen_session()
  if not s then return end
  if not configured() then set_status('No server: tap the gear.') return end
  if not can_ or len_ <= 0 then set_status('This session cannot seek.') return end

  local at = math.floor(tonumber(seconds) or 0)
  if at < 0 then at = 0 end
  if at > len_ then at = len_ end

  -- THE CLOCK STARTS AGAIN FROM HERE: the position is the one just asked for, so the time on the glass is not the old one
  -- plus what the television takes to obey.
  at_    = at
  at_ms_ = sys.time_ms()
  if screen_ == 'control' then refresh_control() end

  local sent, problem, status = send(api.seek(url_, token_, s.id, at))
  if not sent then
    if problem == 'E_UNSUPPORTED' then
      set_status('This system has no http.post: the seek did not go.')
    elseif status == 405 then
      set_status('The server does not take this command.')
    else
      set_status('Not sent: ' .. why_not(problem))
    end
    return
  end
  set_status('Seeking to ' .. clock(at) .. '...')
end

--[[
THIRTY SECONDS EITHER WAY, AND BY `Seek`: `Rewind` and `FastForward` exist on the server and are not what this uses —the
server only hands the word to the client and what a client does with it is its own business—. The position it adds to is the
one on the glass and not the bare word of the server: half a minute after that word, thirty seconds back would land where the
film was and not where it is (25/10).
]]
local function seek_by(delta)
  local s = chosen_session()
  if not s then set_status('No session chosen.') return end
  if not configured() then set_status('No server: tap the gear.') return end
  if not can_ or len_ <= 0 then set_status('This session cannot seek.') return end

  local at = math.floor(pos_now()) + delta
  if at < 0 then at = 0 end
  if at > len_ then at = len_ end
  seek_to(at)
end

--[[
PLAY THIS ON THAT ONE: the list of what is half watched is not a second remote, it is the door that turns a title into a
session —the person picks a title and the television that is already chosen starts it—, and the app moves to the control page
to watch it start (24/10).
]]
local function play_item(r)
  if not r then return end

  local s = chosen_session()
  if not s then
    set_status('Choose a session in Sessions.')
    show('sessions')
    return
  end

  local sent, problem, status = send(api.play(url_, token_, s.id, r.id))
  if not sent then
    if problem == 'E_UNSUPPORTED' then
      set_status('This system has no http.post: the play did not go.')
    elseif status == 405 then
      set_status('The server does not take this command.')
    else
      set_status('Not sent: ' .. why_not(problem))
    end
    return
  end

  set_status('Playing ' .. fit(r.title, 22) .. '...')
  show('control')
  ask_sessions()
end

local function refresh_library()
  if not configured() then set_status('No server: tap the gear.') return end
  local body, problem = ask(api.url(url_, '/Library/Refresh', token_))
  if body == nil then
    set_status('Not asked: ' .. why_not(problem))
    return
  end
  set_status('Library refresh asked.')
end

--[[
THE FIVE THINGS OF THE CHOSEN SESSION, TAKEN FROM THE LAST ANSWER.

The control page belongs to ONE session and the block of `state` has no room for the details of eight, so the detail lives
where the answer left it and the page reads it from there (the same shape the paper had, for the same reason).
]]
load_card = function()
  local d = detail_ and detail_[chosen_]

  if not chosen_session() or not d then
    line_, way_, at_, len_, can_ = '', '', 0, 0, false
    item_id_ = ''
    playing_ = false
    return
  end

  line_ = fit(d.line, 40)
  way_  = fit(d.way, 20)
  at_, len_, can_ = d.at or 0, d.len or 0, d.can == true
  playing_ = (d.paused ~= true) and len_ > 0
  at_ms_   = sys.time_ms()
  mute_, can_mute_ = d.muted == true, d.canMute == true

  if (d.uid or '') ~= '' then user_ = d.uid end

  local was = item_id_
  item_id_ = d.itemId or ''
  if item_id_ ~= was then ask_poster() end
end

-- ---------------------------------------------------------------- the shape of the view (docs/30 § state)

local kLayout = 'v1'
local kStateBytes = 1024
local state_block = nil

local function the_state()
  -- A block the system has taken back cannot be used: it is asked again, and a page without room is built as if it were the
  -- first time (docs/30, docs/75).
  if state_block ~= nil and not state_block:alive() then state_block = nil end
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

save_state = function()
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout, 'page=' .. screen_, 'rpage=' .. tostring(resume_page_) }
  if chosen_session() ~= nil then lines[#lines + 1] = 'chosen=' .. chosen_session().id end
  if shape.focus ~= nil then lines[#lines + 1] = 'focus=' .. tostring(shape.focus) end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local text = block:read(0, kStateBytes)
  if type(text) ~= 'string' then return nil end
  local at = text:find('\0', 1, true)
  if at ~= nil then text = text:sub(1, at - 1) end
  local out = {}
  for line in text:gmatch('[^\r\n]+') do
    local k, v = line:match('^(%w+)=(.*)$')
    if k ~= nil then out[k] = v end
  end
  if out.layout ~= kLayout then return nil end
  return out
end

-- ---------------------------------------------------------------- the strip, the gear and the line of state

local function tab_chosen(index)
  if index == nil or index < 1 or Pages[index] == nil then return end
  show(Pages[index])
  -- CHOOSING A PAGE ASKS FOR WHAT THE PAGE SHOWS: a list from a minute ago is not the list, and a position from a minute ago
  -- is not the position. `Continue` asks its own question when its page is opened (25/10).
  if Pages[index] == 'sessions' or Pages[index] == 'control' then
    ask_sessions()
  elseif Pages[index] == 'main' then
    ask_info()
  end
end

-- The strip is on every page, the settings included: they are not one of its pages, so it keeps saying where a person was
-- and a tab leaves them (21/10).
local function build_strip(width)
  local strip = keep(ui.tabs(width, TabH))
  if strip == nil then return nil end
  strip.items = Titles
  strip.index = page_at(screen_) > 0 and page_at(screen_) or page_at(gear_from_)
  strip.on_change = function(self, index) want(function() tab_chosen(index) end) end
  named.strip = strip
  return strip
end

local function build_gear()
  local gear = keep(ui.button(GearW, GearH))
  if gear == nil then return nil end
  gear.icon = 'settings'
  -- **El clic deixa un `wish` i no fa la feina** (0.199.4): `show()` allibera tots els controls i en construeix de nous, i
  -- fer-ho amb el botó que el sistema té a la mà és desfer la pàgina sota el seu propi clic. És la mateixa porta que la resta
  -- de clics de l'app (`want`), i és el que faltava aquí (l'amo: *«Quan l'app falla no deixa accedir a Settings de l'app»*).
  gear.on_click = function()
    want(function()
      if screen_ == 'settings' then
        show(gear_from_ ~= '' and gear_from_ or 'main')
      else
        gear_from_ = screen_
        show('settings')
      end
    end)
  end
  named.gear = gear
  return gear
end

-- ---------------------------------------------------------------- the page: Server

refresh_main = function()
  if named.info_name ~= nil then
    named.info_name.text = (info_name_ ~= '') and fit(info_name_ .. ' ' .. info_version_, 48) or 'Jellyfin'
  end
  if named.info_counts ~= nil then
    if counts_ then
      named.info_counts.text = string.format('%d movies, %d series', counts_.movies or 0, counts_.series or 0)
    elseif configured() then
      named.info_counts.text = 'The counts are not here yet.'
    else
      named.info_counts.text = 'Tap the gear and type the address and the API key.'
    end
  end
  if named.info_more ~= nil then
    named.info_more.text = counts_ and string.format('%d episodes, %d songs', counts_.episodes or 0, counts_.songs or 0) or ''
  end
  repaint()
end

local function build_main(width, top, bottom)
  local y = top

  named.info_name = keep(ui.label(width - 2 * Margin, ui.line_height(kTitle)))
  if named.info_name ~= nil then
    named.info_name.scale = kTitle
    named.info_name:place(Margin, y)
  end
  y = y + ui.line_height(kTitle) + 10

  named.info_counts = keep(ui.label(width - 2 * Margin, ui.line_height(kBody)))
  if named.info_counts ~= nil then
    named.info_counts.scale = kBody
    named.info_counts:place(Margin, y)
  end
  y = y + ui.line_height(kBody) + 6

  named.info_more = keep(ui.label(width - 2 * Margin, ui.line_height(kBody)))
  if named.info_more ~= nil then
    named.info_more.scale = kBody
    named.info_more:place(Margin, y)
  end

  -- THE ACTIONS OF THIS PAGE, which are two: ask the server again, and ask it to walk its library.
  local h = ui.line_height(kBody) + 20
  local w = (width - 2 * Margin - Gap) // 2
  local by = bottom - h

  named.refresh = keep(ui.button(w, h))
  if named.refresh ~= nil then
    named.refresh.title = 'Refresh'
    named.refresh.scale = kBody
    named.refresh.on_click = function() want(function() ask_info() end) end
    named.refresh:place(Margin, by)
  end

  named.library = keep(ui.button(w, h))
  if named.library ~= nil then
    named.library.title = 'Library'
    named.library.scale = kBody
    named.library.on_click = function() want(refresh_library) end
    named.library:place(Margin + w + Gap, by)
  end

  refresh_main()
end

-- ---------------------------------------------------------------- the page: Sessions

local function build_sessions(width, top, bottom)
  named.count = keep(ui.label(width - 2 * Margin, ui.line_height(kSmall)))
  if named.count ~= nil then
    named.count.scale = kSmall
    named.count.text = #sessions_ == 0 and 'No active sessions.'
                        or string.format('%d session(s).', #sessions_)
    named.count:place(Margin, top)
  end

  local y = top + ui.line_height(kSmall) + 6
  local list = keep(ui.list(width - 2 * Margin, bottom - y))
  if list == nil then return end
  list.row_height = ui.line_height(kBody) + 24
  list.separator = true
  named.list = list
  list:place(Margin, y)

  for i = 1, #sessions_ do
    -- **UNA FILA ÉS UN CLIENT A COMANDAR**, i la pàgina que obre és el control d'aquella sessió. Un `ui.list_entry` és la fila
    -- d'aquesta casa amb el seu `on_click` (docs/30 § `ui.list` amb entrades, apps/files i apps/fonts).
    local entry = ui.list_entry(width - 2 * Margin, list.row_height)
    if entry ~= nil then
      local who = ui.label(width - 2 * Margin - 16, ui.line_height(kBody))
      if who ~= nil then
        who.text = sessions_[i].user
        who.scale = kBody
        who.color = INK
        who.background = PAPER
        who:place(8, 2)
        entry:add(who)
      end

      local state = ui.label(width - 2 * Margin - 16, ui.line_height(kSmall))
      if state ~= nil then
        state.text = (api ~= nil) and api.state(sessions_[i]) or ''
        state.scale = kSmall
        state.color = GREY
        state.background = PAPER
        state:place(8, 2 + ui.line_height(kBody))
        entry:add(state)
      end

      local mine = i
      entry.on_click = function()
        chosen_ = mine
        want(function()
          load_card()
          show('control')
          ask_sessions()
        end)
      end
      entry.on_focus = function(self, on)
        if on then list.marked = mine end
      end
      if chosen_ == i then list.marked = i end
      list:add(entry)
    end
  end
end

-- ---------------------------------------------------------------- the page: Continue

--[[
A CARD OF WHAT IS HALF WATCHED: THE PICTURE ON THE LEFT AND THE STORY ON THE RIGHT (24/10, l'amo: *«La imatge de la caràtula
a un bon tamany / A la dreta, el títol en gran / Sota el titol, la barra de % de vist / Sota la barra, el temps restant / Sota
el temps restant: mes info del video»*).

The paper drew the card with its own pencil because it had no widget for it. Here the card is a row of the system's list (a
`ui.list_entry`), the picture is a `ui.canvas` that paints the file, and the bar of what is seen is `ui.progress`: the same
card with the controls of this house (docs/45 § 3 and 4).
]]
local function add_card(list, index, r, width, height, n)
  local entry = ui.list_entry(width, height)
  if entry == nil then return end

  local pad = CardPad
  local poster_h = height - 2 * pad
  local poster_w = (poster_h * 2) // 3
  if poster_w > PosterW then poster_w, poster_h = PosterW, PosterH end

  local path = card_path(r.id)
  local picture = keep(ui.canvas(poster_w, poster_h))
  if picture ~= nil then
    picture:place(pad, pad)
    -- **El fitxer es mira en el moment de pintar i no quan es fa el control**: a picture that arrives after the card was
    -- built —the page asks for its two covers when it is opened— is a picture that a canvas with a decision taken at build
    -- time would never show (docs/30, docs/50).
    picture.on_draw = function(self)
      self:clear(PAPER)
      if fs.stat(path) ~= nil then
        self:image(0, 0, path, poster_w, poster_h)
      else
        self:frame(0, 0, poster_w, poster_h, GREY)
      end
    end
    entry:add(picture)
  end

  local col_x = pad + poster_w + ColGap
  local col_w = width - col_x - pad
  local y = pad

  local title = ui.label(col_w, ui.line_height(kBody))
  if title ~= nil then
    title.text = fit(r.title, 40)
    title.scale = kBody
    title.color = INK
    title.background = PAPER
    title:place(col_x, y)
    entry:add(title)
  end
  y = y + ui.line_height(kBody) + 8

  local bar = ui.progress(col_w - 74, CardBarH)
  if bar ~= nil then
    bar.value = r.pct or 0
    bar:place(col_x, y)
    entry:add(bar)
  end

  local pct = ui.label(70, ui.line_height(kSmall))
  if pct ~= nil then
    pct.text = string.format('%d %%', r.pct or 0)
    pct.scale = kSmall
    pct.color = INK
    pct.background = PAPER
    pct:place(col_x + col_w - 70, y)
    entry:add(pct)
  end
  y = y + CardBarH + 8

  local left = ui.label(col_w, ui.line_height(kSmall))
  if left ~= nil then
    left.text = string.format('%s left', clock(r.left or 0))
    left.scale = kSmall
    left.color = INK
    left.background = PAPER
    left:place(col_x, y)
    entry:add(left)
  end
  y = y + ui.line_height(kSmall) + 4

  local more = ui.label(col_w, ui.line_height(kSmall))
  if more ~= nil then
    more.text = fit(r.line or '', 48)
    more.scale = kSmall
    more.color = GREY
    more.background = PAPER
    more:place(col_x, y)
    entry:add(more)
  end

  entry.on_click = function()
    picked_ = n
    want(function() play_item(resume_[n]) end)
  end
  if n == picked_ then list.marked = index end

  list:add(entry)
end

local function build_continue(width, top, bottom)
  -- Les caràtules de la pàgina que es veurà es demanen abans de fer els controls: un canvas que es dibuixa no pot anar a la
  -- xarxa (docs/30, docs/50).
  fill_pictures(resume_page_)

  named.count = keep(ui.label(width - 2 * Margin, ui.line_height(kSmall)))
  if named.count ~= nil then
    named.count.scale = kSmall
    named.count.text = asking_resume_ and 'Asking what is half watched...'
                          or (#resume_ == 0 and 'Nothing to continue.' or string.format('%d to continue.', #resume_))
    named.count:place(Margin, top)
  end

  local y = top + ui.line_height(kSmall) + 6
  local list = keep(ui.list(width - 2 * Margin, bottom - y))
  if list == nil then return end
  local row = math.min(CardH, (bottom - y - Gap) // 2)
  list.row_height = row
  named.list = list
  list:place(Margin, y)

  local per_page = math.max(1, (bottom - y) // (row + 1))
  local pages = math.max(1, (#resume_ + per_page - 1) // per_page)
  if resume_page_ > pages then resume_page_ = pages end
  if resume_page_ < 1 then resume_page_ = 1 end
  list.page = resume_page_

  list.on_page = function(self, page)
    resume_page_ = page
    save_state()
    -- LES CARÀTULES DE LA PÀGINA NOVA es demanen al bucle i la pàgina no es refà: els canvas d'aquesta pàgina miren el fitxer
    -- en el moment de pintar, o sigui que el que arriba es veu sense tornar a fer cap control (i tornar a fer-los des d'un
    -- ganxo d'un control és el que la casa diu que no es fa, docs/30).
    want(function()
      fill_pictures(page)
      ui.present('gc16')
    end)
  end

  for i = 1, #resume_ do
    add_card(list, i, resume_[i], width - 2 * Margin, row, i)
  end
end

-- ---------------------------------------------------------------- the page: Control

local function times_text()
  local at = math.floor(pos_now() + 0.5)
  if not can_ or len_ <= 0 then return 'Live' end
  return string.format('%s   %s   -%s', clock(at), clock(len_), clock(len_ - at))
end

refresh_control = function()
  if screen_ ~= 'control' then return end
  local s = chosen_session()

  if named.title ~= nil then
    local head
    if s == nil then
      head = 'Choose a session in Sessions.'
    elseif s.title ~= '' then
      head = s.title
    else
      head = string.format('%s on %s', s.user, s.device ~= '' and s.device or '?')
    end
    named.title.text = fit(head, 34)
  end

  if named.line ~= nil then named.line.text = (line_ ~= '' and line_ or way_) end
  if named.times ~= nil then named.times.text = times_text() end

  if named.bar ~= nil then
    named.bar.max = math.max(1, math.floor(len_ / 60 + 0.5))
    named.bar.value = math.floor(pos_now() / 60 + 0.5)
  end

  if named.play ~= nil then named.play.icon = playing_ and 'pause' or 'play' end
  if named.mute ~= nil then named.mute.title = mute_ and 'Unmute' or 'Mute' end
  repaint()
end

local function build_control(width, top, bottom)
  local wide = width >= view.height
  local poster_w, poster_h, col_x, col_y, col_w

  if wide then
    poster_w = (width * 45) // 100
    poster_h = bottom - top
    col_x, col_y = poster_w + Gap + Margin, top
    col_w = width - col_x - Margin
  else
    poster_w, poster_h = width - 2 * Margin, math.min(ArtH, ((bottom - top) * 45) // 100)
    col_x, col_y, col_w = Margin, top + poster_h + Gap, width - 2 * Margin
  end

  local path = poster_path()
  local there = item_id_ ~= '' and fs.stat(path) ~= nil
  named.picture = keep(ui.canvas(poster_w, poster_h))
  if named.picture ~= nil then
    named.picture:place(wide and 0 or Margin, top)
    local cw, ch = poster_w, poster_h
    named.picture.on_draw = function(self)
      self:clear(PAPER)
      if not there then
        self:frame(2, 2, cw - 4, ch - 4, GREY)
        return
      end
      local info = ui.image_info(path)
      local w, h = info and info.width or 0, info and info.height or 0
      if w < 1 or h < 1 then return end
      if w > cw or h > ch then
        local k = math.min(cw / w, ch / h)
        w, h = math.floor(w * k + 0.5), math.floor(h * k + 0.5)
      end
      self:image(math.max(0, (cw - w) // 2), math.max(0, (ch - h) // 2), path, w, h)
    end
  end

  local y = col_y
  named.title = keep(ui.label(col_w, ui.line_height(kBody)))
  if named.title ~= nil then
    named.title.scale = kBody
    named.title.color = INK
    named.title:place(col_x, y)
  end
  y = y + ui.line_height(kBody) + 4

  named.line = keep(ui.label(col_w, ui.line_height(kSmall)))
  if named.line ~= nil then
    named.line.scale = kSmall
    named.line.color = GREY
    named.line:place(col_x, y)
  end
  y = y + ui.line_height(kSmall) + 8

  --[[
  THE BAR AND ITS THREE TIMES: the slider IS the bar —a finger on its tube puts the position where it fell, which is the
  draggable bar of the paper— and the line under it says how much has gone, how much there is and how much is left (24/10,
  l'amo: *«posa també una barra de progrés amb el temps total i el temps transcorregut, també el temps perquè acabi»*). The
  number of the slider is in minutes because a slider shows its value and a value in seconds is a number nobody reads.
  ]]
  named.bar = keep(ui.slider(col_w, 44))
  if named.bar ~= nil then
    named.bar.min = 0
    named.bar.max = math.max(1, math.floor(len_ / 60 + 0.5))
    named.bar.step = 1
    named.bar.suffix = ' min'
    named.bar.scale = kSmall
    named.bar.value = math.floor(pos_now() / 60 + 0.5)
    named.bar.on_change = function(self, value)
      want(function() seek_to((tonumber(value) or 0) * 60) end)
    end
    named.bar:place(col_x, y)
  end
  y = y + 44 + 4

  named.times = keep(ui.label(col_w, ui.line_height(kSmall)))
  if named.times ~= nil then
    named.times.scale = kSmall
    named.times.color = INK
    named.times:place(col_x, y)
  end
  y = y + ui.line_height(kSmall) + 10

  -- THE SEVEN KEYS, with the two of thirty and the silence only when the session can do them: a button that does nothing is
  -- worse than no button (24/10). The middle one is the filled one and it wears the mark of the system.
  local room = bottom - y
  local keys_h = math.min(CtlBig, math.max(56, (room - CtlSmall - 24) // 2))
  local each = (col_w - 4 * Gap) // 5
  local keys = {
    { name = 'prev', title = '|<', action = function() command('PreviousTrack') end },
    { name = 'rew',  title = '<<', action = function() seek_by(-30) end },
    { name = 'play', title = nil,  action = function() command('PlayPause') end },
    { name = 'fwd',  title = '>>', action = function() seek_by(30) end },
    { name = 'next', title = '>|', action = function() command('NextTrack') end },
  }

  for i, one in ipairs(keys) do
    local button = keep(ui.button(each, keys_h))
    if button ~= nil then
      if one.title ~= nil then
        button.title = one.title
        button.scale = kBody
      else
        button.icon = playing_ and 'pause' or 'play'
        button.primary = true
      end
      button.on_click = function() want(one.action) end
      button.on_focus = function(self, on)
        if on then shape.focus = one.name end
      end
      button:place(col_x + (i - 1) * (each + Gap), y)
      named[one.name] = button
    end
  end
  y = y + keys_h + 12

  local small_h = math.min(CtlSmall, math.max(44, bottom - y))
  local half = (col_w - Gap) // 2

  named.stop = keep(ui.button(half, small_h))
  if named.stop ~= nil then
    named.stop.title = 'Stop'
    named.stop.scale = kBody
    named.stop.on_click = function() want(function() command('Stop') end) end
    named.stop:place(col_x, y)
  end

  named.mute = keep(ui.button(half, small_h))
  if named.mute ~= nil then
    named.mute.title = mute_ and 'Unmute' or 'Mute'
    named.mute.scale = kBody
    named.mute.on_click = function() want(toggle_mute) end
    named.mute:place(col_x + half + Gap, y)
  end

  refresh_control()
end

-- ---------------------------------------------------------------- the page: Settings

local function build_settings(width, top)
  local y = top

  local url_lbl = keep(ui.label(width - 2 * Margin, ui.line_height(kSmall)))
  if url_lbl ~= nil then
    url_lbl.text = 'Server (http://host:8096, no https)'
    url_lbl.scale = kSmall
    url_lbl:place(Margin, y)
  end
  y = y + ui.line_height(kSmall) + 4

  named.url_box = keep(ui.textbox(width - 2 * Margin, 64))
  if named.url_box ~= nil then
    named.url_box.text = url_
    named.url_box.scale = kBody
    named.url_box:place(Margin, y)
  end
  y = y + 64 + 12

  local key_lbl = keep(ui.label(width - 2 * Margin, ui.line_height(kSmall)))
  if key_lbl ~= nil then
    key_lbl.text = 'API key (Dashboard > API keys)'
    key_lbl.scale = kSmall
    key_lbl:place(Margin, y)
  end
  y = y + ui.line_height(kSmall) + 4

  named.token_box = keep(ui.textbox(width - 2 * Margin, 64))
  if named.token_box ~= nil then
    named.token_box.text = token_
    named.token_box.scale = kBody
    named.token_box.mask = true
    named.token_box:place(Margin, y)
  end
  y = y + 64 + 12

  -- ONE DOOR: save, and go back to the server page with the new address. The app also saves when it leaves this page, so
  -- nothing typed is lost (21/10).
  local save = keep(ui.button(width - 2 * Margin, ui.line_height(kBody) + 20))
  if save ~= nil then
    save.title = 'Save'
    save.scale = kBody
    save.primary = true
    save.on_click = function()
      want(function()
        if save_settings() then
          show('main')
          ask_info()
        end
      end)
    end
    save:place(Margin, y)
  end
end

--[[
THE TWO SETTINGS, SAVED TOGETHER.

The address is completed and `https` is refused by name —this system does not speak TLS and a handshake that never happens
would look like a broken server— and the key is the only thing that ever travels in the address (`api.base()`).
]]
save_settings = function()
  if api == nil then
    set_status("The app's own modules are missing.")
    return false
  end

  local typed_url = named.url_box ~= nil and named.url_box.text or url_
  local base, why_base = api.base(typed_url)
  if not base then
    set_status('Address: ' .. tostring(why_base))
    return false
  end

  local key = fit(named.token_box ~= nil and named.token_box.text or token_, 40)
  if key == '' then
    set_status('The API key is empty.')
    return false
  end

  url_, token_ = base, key

  -- A NEW ADDRESS IS A NEW SERVER: what the old one said is not the new one's answer, and keeping it on the glass would be
  -- a lie with a number in it.
  counts_, info_name_, info_version_ = nil, '', ''
  refresh_main()

  if not write_settings() then return false end

  set_status('Saved.')
  return true
end

-- ---------------------------------------------------------------- the pages, built and shown

local function page_top()
  return Margin + GearH + Gap + TabH + Gap
end

local function page_bottom()
  return view.height - Margin - 2 * ui.line_height(kSmall) - 6
end

show = function(which)
  if page_at(which) == 0 and which ~= 'settings' then which = 'main' end

  page_ready_ = false
  give_back()
  screen_ = which

  -- **El paper es neteja abans de fer-hi res** (trap 5 de docs/45): `release()` torna el control al sistema al proper
  -- `present` i el que hi havia pintat es queda al vidre on la pàgina nova no hi pinta res —o sigui la pàgina que se'n va
  -- sota la que arriba—, que és el que es veia com les pestanyes barrejades (l'amo: *«Les pestanyes "Server" i "Session"
  -- estan barrejades»*). Un `ui.clear(15)` és tota la vista de paper i els controls es tornen a dibuixar a sobre.
  ui.clear(PAPER)

  local width = view.width

  -- The strip and the gear are on every page: the strip says where a person is and the gear is the way in and out of the
  -- settings (21/10).
  local strip = build_strip(width - 2 * Margin)
  if strip ~= nil then strip:place(Margin, Margin + GearH + Gap) end
  local gear = build_gear()
  if gear ~= nil then gear:place(width - Margin - GearW, Margin) end

  local top, bottom = page_top(), page_bottom()

  if which == 'settings' then
    build_settings(width, top)
  elseif which == 'sessions' then
    build_sessions(width, top, bottom)
  elseif which == 'continue' then
    build_continue(width, top, bottom)
  elseif which == 'control' then
    build_control(width, top, bottom)
  else
    build_main(width, top, bottom)
  end

  -- The line of state is at the bottom of every page: what the app is doing and, when something failed, why (20/10).
  named.status = keep(ui.label(width - 2 * Margin, 2 * ui.line_height(kSmall)))
  if named.status ~= nil then
    named.status.scale = kSmall
    named.status.text = fit(status_, 96)
    named.status:place(Margin, view.height - Margin - 2 * ui.line_height(kSmall))
  end

  if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end

  ui.present('gc16')
  page_ready_ = true
  save_state()
end

-- ---------------------------------------------------------------- the life of the script

local function find_the_shape()
  local at = load_state()
  if at == nil then return end

  if at.page ~= nil and page_at(at.page) > 0 then screen_ = at.page end
  if tonumber(at.rpage) ~= nil then resume_page_ = math.max(1, math.floor(tonumber(at.rpage))) end
  if at.chosen ~= nil then saved_chosen_ = at.chosen end
  if at.focus ~= nil then shape.focus = at.focus end
end

function on_resize(w, h)
  view.width, view.height = w, h
  show(screen_)
end

function on_pause()
  -- AN ADDRESS THAT WAS TYPED IS NOT THROWN AWAY because the app was left from the keyboard: it is written, and the next
  -- start asks the server with it (21/10).
  if screen_ == 'settings' and named.url_box ~= nil and named.token_box ~= nil then
    save_settings()
  end
  save_state()
end

function on_save() save_state() end
function on_close() save_state() end

read_settings()
find_the_shape()
show(screen_)

local function on_start_question()
  if json == nil or api == nil then
    set_status("The app's own modules are missing.")
    return
  end
  if configured() then
    set_status('Asking the server...')
    ask_info()
  else
    set_status('No server: tap the gear.')
  end
end

on_start_question()

--[[
THE CLOCK OF THE PAGE, AND NOTHING ELSE.

Every `PollMs` the list is asked again QUIETLY, so that the position on the glass is set back to the server's word: the two
clocks —the television's and this app's— were never promised to run at the same speed, and this is the only thing that fixes
that. It paints nothing on its own (24/10, l'amo: *«No redibuixis el tab "Control" cada segon!»*): the time becomes pixels
when something else asks for a pass —an answer, a command, a finger on the bar—.
]]
local last_poll = sys.time_ms()

while true do
  -- **El que un ganxo ha demanat es fa aquí**: aquesta és l'única coroutine que pot esperar una resposta de la xarxa, i el que
  -- arriba d'un `on_click` és un `wish` i no una crida (docs/30).
  if wish ~= nil then
    local job = wish
    wish = nil
    local ok, why = pcall(job)
    if not ok then say('what was asked did not finish: ' .. tostring(why)) end
  end

  sys.delay(100):await()

  if screen_ == 'control' and chosen_session() ~= nil and configured() then
    local now = sys.time_ms()
    if (now - last_poll) >= PollMs then
      last_poll = now
      ask_sessions(true)
      refresh_control()
      ui.present('gc16')
    end
  end
end
