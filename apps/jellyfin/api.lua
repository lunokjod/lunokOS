-- ============================================================================
--  Jellyfin :: api.lua
-- ============================================================================
--  THE ADDRESSES OF A JELLYFIN SERVER AND WHAT ITS ANSWERS MEAN, ON ITS OWN
--  BECAUSE IT HAS NO SCREEN AND NO RADIO: `require('api')` returns this module,
--  the file returns it, and the screen only asks.
--
--  WHERE THE TOKEN GOES, AND WHY IT GOES IN THE ADDRESS: `http.get` writes its
--  own headers and has no door for an `Authorization`, so the only way a key
--  travels here is the one Jellyfin reads from the query string. WHICH NAME IT
--  HAS IS NOT A DETAIL: its `AuthorizationContext` reads `ApiKey` ALWAYS, and
--  reads `api_key` only when `EnableLegacyAuthorization` is on -a flag with no
--  default, and the 12.x migration `DisableLegacyAuthorization` writes it off,
--  so `api_key` is **deprecated and refused** on a current server-. `ApiKey` is
--  the name that works on 10.9 and on 12.x, and it is the only one this module
--  writes. It is a key read from `/data/jellyfin/settings.txt` (plain text, on
--  the card or the flash): anybody who can read the card can command the
--  server, and that is said in `Jellyfin.md` and not discovered later.
--
--  NOTHING IS URL-ENCODED: an API key is 32 hexadecimal characters and a
--  session id is 32 more, and that is the whole alphabet that travels. A server
--  address with a space in it is a typo, and the system's own address check
--  refuses it with its reason.
-- ============================================================================

local M = {}

-- A text, or "" when the JSON brought `null` or something else.
local function str(value)
  if type(value) == "string" then return value end
  return ""
end

--[[
Cuts a text to a number of BYTES without leaving half a character.

The limits of the system are in bytes -the text of a box or a row of a list
(`ListBox::BufLen` is 40 allà)- and a name cut in the middle of a UTF-8
sequence is a row of rubbish on the glass. Aquí els textos poden ser més llargs
-el bloc de l'estat són 1 024 B i la fila d'una llista no té aquest sostre-,
però el que es veu en una línia sí que té un sostre de píxels, i el número
d'aquesta funció és el que diu quant en cap.
]]
function M.fit(text, n)
  text = str(text)

  if #text <= n then return text end

  local cut = text:sub(1, n)
  local last = cut:byte(-1)

  while last and last >= 0x80 and last < 0xC0 do
    cut  = cut:sub(1, -2)
    last = cut:byte(-1)
  end

  if last and last >= 0xC0 then cut = cut:sub(1, -2) end

  return cut
end

--[[
The address the user wrote, as the base of every request.

A bare `host:port` is completed with `http://` because that is what everybody
types; `https://` is refused BY NAME, because this system does not speak TLS
and a handshake that never happens would look like a broken server.

@return the base without a trailing slash, or nil and why
]]
function M.base(raw)
  local text = str(raw):gsub("^%s+", ""):gsub("%s+$", "")

  if text == "" then return nil, "there is no address" end

  -- The scheme is a scheme whatever its case, and a person typing the address
  -- on a keyboard of a paper board types it however it comes out.
  local lower = text:lower()

  if lower:sub(1, 8) == "https://" then
    return nil, "https is closed: use http"
  end

  if lower:sub(1, 7) ~= "http://" then
    if text:find("://", 1, true) then return nil, "only an http:// address" end
    text = "http://" .. text
  end

  text = text:gsub("/+$", "")

  if text == "http://" then return nil, "there is no address" end

  return text
end

-- One address of the server, with the token when there is one.
function M.url(base, path, token)
  token = str(token)

  if token == "" then return base .. path end

  local sep = path:find("?", 1, true) and "&" or "?"

  return base .. path .. sep .. "ApiKey=" .. token
end

--[[
A playback command of one session.

The commands are the server's own names (`PlayPause`, `Stop`, `NextTrack`,
`PreviousTrack`) and the route is `POST /Sessions/{id}/Playing/{command}`:
a verb with no body. **AQUEST SISTEMA NOMÉS SAP FER GET** (`http.get` i
`http.download`, i cap porta per a un `POST`), o sigui que el que l'app envia
és el mateix camí amb un `GET` i un servidor de debò contesta **405**: es queda
dit aquí perquè qui ho vegi no busqui el defecte a l'adreça.

Its address is the reason the ceiling of a URL is 192 and not 96 -a token and a
session id are 32 characters each-.
]]
function M.command(base, token, id, name)
  return M.url(base, "/Sessions/" .. id .. "/Playing/" .. name, token)
end

--[[
A GENERAL COMMAND, WHICH IS ANOTHER ROUTE WITH THE SAME SHAPE.

`/Sessions/{id}/Command/{name}` and not `/Playing/{name}`: the playstate ones
(`Pause`, `Seek`...) are one family and the general ones (`Mute`, `Unmute`,
`ToggleMute`, `SetVolume`, `DisplayMessage`, `MoveUp`...) are another, and the
server keeps them in two enums. They are told apart by the ROUTE, and a client
only obeys the ones it has declared in `SupportedCommands` -which is why this
app asks before it draws the button-.
]]
function M.general(base, token, id, name)
  return M.url(base, "/Sessions/" .. id .. "/Command/" .. name, token)
end

--[[
WHAT IS HALF WATCHED, AND THE ONE THING THAT PLAYS SOMETHING NEW.

`/UserItems/Resume` needs a `userId`, and with an API key there is none in the
address: the server looks it up and answers 404 without one. The user this app
knows is the `UserId` of a session, so the list of sessions comes first.

MEASURED FOR THE LENGTH OF THE ADDRESS (the queue's ceiling is 191 chars):
`http://host:port` + `/UserItems/Resume` + `?userId=` + 32 + `&limit=8` +
`&enableUserData=true` + `&enableImages=false` + `&ApiKey=` + 32 = **165** in the
worst case of a long host. `enableUserData` is what brings
`UserData.PlayedPercentage`, and with it the row says how much of it is left;
`enableImages=false` takes the image tags out of a DTO that this app cannot use
(it has no JPEG).
]]
function M.resumeUrl(base, token, userId)
  return M.url(base, "/UserItems/Resume?userId=" .. userId ..
                     "&limit=8&enableUserData=true&enableImages=false", token)
end

--[[
PLAY THIS, ON THAT CLIENT.`/Sessions/{id}/Playing?playCommand=PlayNow&itemIds={guid}` -the play command
needs no body, like every other order of this app-, and it is the door that
turns a list into a remote: the film starts on the television, not here.
`PlayNext`, `PlayLast`, `PlayShuffle` and `PlayInstantMix` are the same door
with another word.
]]
function M.play(base, token, id, itemId)
  return M.url(base, "/Sessions/" .. id ..
                     "/Playing?playCommand=PlayNow&itemIds=" .. itemId, token)
end

--[[
THE PICTURE OF WHAT IS PLAYING, AND IT HAS TO SAY PNG.

`/Items/{itemId}/Images/Primary` serves whatever the library has -almost always
a JPEG, which this system does not decode-, and `format=png` is the parameter
that makes the server turn it into a PNG before answering. Without it the server
looks at the `Accept` header, which the HTTP client of this board does not send,
and falls back to JPEG by name (`ImageController.GetClientSupportedFormats()`).

Measured for the length of the address (the queue's ceiling is 191 characters):
21 (host) + `/Items/` + 32 + `/Images/Primary` + the three numbers + `&ApiKey=`
+ 32 = **155**.

`maxWidth` and `maxHeight` are the box the control page paints, so it is the
server that shrinks the picture: a 120 x 180 poster of a 4K film is tens of
kilobytes to move and ten thousand bytes of four-bit rows to keep.
]]
function M.poster(base, token, itemId, w, h, kind, fill)
  --[[
  THE TYPE OF THE PICTURE IS ASKED AND NOT ASSUMED (24/10).

  `ImageType` has thirteen names -`Primary`, `Art`, `Backdrop`, `Banner`, `Logo`,
  `Thumb`, `Disc`, `Box`, `Menu`, `Chapter`, `BoxRear`, `Profile` and the obsolete
  `Screenshot`- and this app uses two: **`Primary`**, which is the poster of a
  film and the still of an episode, and **`Backdrop`**, which is a scene of the
  film in 16:9 -the shape a television has-.

  AND THE TWO NUMBERS ARE A BOX AND NOT A SIZE: the server fits the picture
  inside `maxWidth` x `maxHeight` keeping the shape it has, so a poster asked in a
  320 x 180 box comes back 120 x 180. That is why the page reads the size it got
  (`ui.image_info()` says so) and does not assume the one it asked for.

  AND THERE ARE TWO WAYS OF ASKING (25/10): `maxWidth`/`maxHeight` FIT -the whole
  picture, its shape kept, inside the box- and `fillWidth`/`fillHeight` FILL -the
  box, the shape cropped from the middle-. The grid wants the first, because a cell
  of 248 x 344 holds a poster of 200 x 300 with its shape; the control page wants
  the second, because its picture is the width of the glass -540 x 360, 3:2- and a
  poster fitted in that box comes back 240 x 360 with a margin on each side.
  ]]
  local type_ = kind or "Primary"
  local box

  if fill == true then
    box = "fillWidth=" .. string.format("%d", w) .. "&fillHeight=" ..
          string.format("%d", h)
  else
    box = "maxWidth=" .. string.format("%d", w) .. "&maxHeight=" ..
          string.format("%d", h)
  end

  return M.url(base, "/Items/" .. itemId .. "/Images/" .. type_ .. "?" .. box ..
                     "&format=png", token)
end

--[[
Whether a session says it can be muted.

`SupportedCommands` is a list of `GeneralCommandType` and it is the client's own
promise; the three names here are the ones that fill the same hole (a client may
have `ToggleMute` and not `Mute`). A session that says none of them gets no
button, which is better than a button that does nothing.
]]
function M.canMute(list)
  if type(list) ~= "table" then return false end

  for i = 1, #list do
    local c = str(list[i])

    if c == "Mute" or c == "Unmute" or c == "ToggleMute" then return true end
  end

  return false
end

--[[
A command that carries a POSITION: `Seek`.

The route is the one of `M.command()` and the position travels in the query as
TICKS -`[FromQuery] long? seekPositionTicks`, from the server's own controller
(`SessionController.SendPlaystateCommand`)-, where a tick is 100 ns: one second
is 10 000 000 of them.

AND THAT NUMBER DOES NOT FIT IN AN INTEGER OF THIS LUA. The engine is built
with `LUA_32BITS` and 2^31 ticks are **3 minutes and 35 seconds** of film, so a
`3600 * 10000000` here is a negative number and the server would seek to before
the beginning of the film and answer 204, which is the worst answer there is:
the one that says everything went well. **The zeros are appended as TEXT**: the
seconds are written with `%d` and seven zeros follow, which is the same number
and exact -`3600` and `"0000000"` make `36000000000`-.

@return the address, with the token, of a POST with no body
]]
function M.seek(base, token, id, seconds)
  local s = math.floor((tonumber(seconds) or 0) + 0.5)

  if s < 0 then s = 0 end

  return M.url(base, "/Sessions/" .. id ..
               "/Playing/Seek?seekPositionTicks=" ..
               string.format("%d", s) .. "0000000", token)
end

--[[
The name and the version of the server, from `/System/Info/Public`.

The PUBLIC door is the one the app asks: `/System/Info` answered **9 250 B**
on the real server (the changelog of every installed plugin) and 4 096 is all
the queue keeps. The token is proved by `/Items/Counts`, not here -a public
answer says nothing about a key-. The by-name reading is kept because a cut is
always possible and the two values are the only thing that is wanted.
]]
function M.info(json, text)
  local value = json.decode(text)
  local out   = { name = "", version = "" }

  if type(value) == "table" then
    out.name    = str(value.ServerName)
    out.version = str(value.Version)
  end

  if out.name == "" then
    out.name = text:match('"ServerName"%s*:%s*"([^"]*)"') or ""
  end

  if out.version == "" then
    out.version = text:match('"Version"%s*:%s*"([^"]*)"') or ""
  end

  if out.name == "" and out.version == "" then
    return nil, "the answer does not say what server it is"
  end

  return out
end

-- One number of a JSON object, by its name.
local function count(json, text, key)
  local value = json.decode(text)

  if type(value) == "table" and type(value[key]) == "number" then
    return math.floor(value[key])
  end

  local found = text:match('"' .. key .. '"%s*:%s*(%d+)')

  return found and tonumber(found) or nil
end

-- `GET /Items/Counts`: what the library holds, which is what a server is.
function M.counts(json, text)
  local out = {
    movies   = count(json, text, "MovieCount"),
    series   = count(json, text, "SeriesCount"),
    episodes = count(json, text, "EpisodeCount"),
    songs    = count(json, text, "SongCount"),
  }

  if not out.movies and not out.series and not out.episodes and not out.songs then
    return nil, "the answer has no counts"
  end

  return out
end

-- Seconds out of a tick count.
--
-- The count arrives as a NUMBER and this engine has 32-bit integers and 32-bit
-- floats (`LUA_32BITS`): a two hour film is 7.2e10 ticks, which only lives as a
-- float, and its last digits are gone. Half a second of rounding is what that
-- costs, and a seek is not a frame-accurate operation.
local function secsFrom(ticks)
  if type(ticks) ~= "number" or ticks <= 0 then return 0 end

  return math.floor(ticks / 10000000 + 0.5)
end

-- "S01E03", or "E03" for an episode with no season, or "".
local function episode(item)
  local e = item.IndexNumber

  if type(e) ~= "number" then return "" end

  local s = item.ParentIndexNumber

  if type(s) == "number" then
    return string.format("S%02dE%02d", math.floor(s), math.floor(e))
  end

  return string.format("E%02d", math.floor(e))
end

--[[
WHAT IS PLAYING, IN THE LINES THE SCREEN HAS ROOM FOR.

The label of the control page is **128 bytes** (`Label::MaxText`) and that is
the whole budget: three lines of 40 and their two newlines. So the fields are
chosen and CUT here, keeping what a person would keep if one had to go: the
title (which the caller already has, it is the row of the list), the identity
of the thing -`S01E03 · Series · 1958 · PG-13`, or the album of a song, or
`Movie · 1958 · PG-13`-, and the way it travels plus how much of it is done.

The three numbers are SECONDS, never ticks: `M.seek()` says why a tick count is
not something this engine can hold.
]]
function M.nowPlaying(item, play)
  local out = { line = "", way = "", at = 0, len = 0, can = false }

  if type(item) ~= "table" then return out end

  local parts = {}
  local ep    = episode(item)

  if ep ~= "" then parts[#parts + 1] = ep end

  local series = str(item.SeriesName)
  local album  = str(item.Album)

  if series ~= "" then parts[#parts + 1] = series end
  if album ~= "" then parts[#parts + 1] = album end

  local year = item.ProductionYear

  if type(year) == "number" then
    parts[#parts + 1] = string.format("%d", math.floor(year))
  end

  local rating = str(item.OfficialRating)

  if rating ~= "" then parts[#parts + 1] = rating end

  if #parts == 0 then
    local kind = str(item.MediaType)

    if kind ~= "" then parts[1] = kind end
  end

  out.line = M.fit(table.concat(parts, " · "), 40)

  if type(play) == "table" then
    out.way = M.fit(str(play.PlayMethod), 20)
    out.at  = secsFrom(play.PositionTicks)
    out.can = play.CanSeek == true

    -- WHETHER IT IS MOVING, which is what decides if the page's clock runs: the
    -- time of a paused session does not move, and neither does the one this app
    -- paints (24/10, the owner: *«el temps corri segons si el video está en play
    -- o pausa»*).
    out.paused = play.IsPaused == true

    -- El silenci és un estat que el servidor ja diu, i és el que fa que el
    -- botó digui Mute o Unmute en comptes de ser un botó que no se sap què
    -- fa fins que es prem.
    out.muted = play.IsMuted == true
  end

  out.len = secsFrom(item.RunTimeTicks)

  return out
end

-- THE SECOND ROAD, AND IT IS THE ONE THAT WORKS ON THE BOARD (20/10).
--
-- `arrayPrefix()` is the exact answer and it is the one that is used when the
-- body came whole. But a body that came HALF -a `GET` is cut at **16 384 B** by
-- `http.get`, and a file can come short for a reason of the card- has no whole
-- object in it, and a reader that insists on whole objects reads NOTHING out of
-- a good answer.
--
-- So the fields are looked for BY NAME inside each session, from the session's
-- `"PlayState"` to the next one's. Measured on the real server (20/10, a body
-- of 17 153 B): inside a session, `PlayState` comes FIRST and the things the
-- screen needs follow it -`PositionTicks` at **15**, `CanSeek` **43**,
-- `IsPaused` **58**, `PlayMethod` **206**, `Id` **959**, `UserName` **1 043**,
-- `DeviceName` **1 190**, `Name` **1 230**, `RunTimeTicks` **2 574**- and all of
-- that is inside the **4 096** a `GET` keeps. It is Jellyfin's own property
-- order, and it is the reason this road gives the session that is playing even
-- when the answer was cut.
local function field(region, key, quoted)
  if quoted then
    return region:match('"' .. key .. '"%s*:%s*"([^"]*)"')
  end

  return region:match('"' .. key .. '"%s*:%s*(%d+)')
end

local function lineFrom(region)
  local parts = {}

  local e = tonumber(field(region, "IndexNumber"))
  local s = tonumber(field(region, "ParentIndexNumber"))

  if e then
    if s then
      parts[#parts + 1] = string.format("S%02dE%02d", s, e)
    else
      parts[#parts + 1] = string.format("E%02d", e)
    end
  end

  local series = field(region, "SeriesName", true) or ""
  local album  = field(region, "Album", true) or ""

  if series ~= "" then parts[#parts + 1] = series end
  if album ~= "" then parts[#parts + 1] = album end

  local year = tonumber(field(region, "ProductionYear"))

  if year then parts[#parts + 1] = string.format("%d", year) end

  local rating = field(region, "OfficialRating", true) or ""

  if rating ~= "" then parts[#parts + 1] = rating end

  if #parts == 0 then
    local kind = field(region, "MediaType", true) or ""

    if kind ~= "" then parts[1] = kind end
  end

  return M.fit(table.concat(parts, " · "), 40)
end

local function scanSessions(text)
  local starts, from = {}, 1

  while true do
    local a = text:find('"PlayState"', from, true)
    if not a then break end

    starts[#starts + 1] = a
    from = a + 1
  end

  local out, detail = {}, {}

  for i = 1, #starts do
    local a   = starts[i]
    local b   = starts[i + 1] and (starts[i + 1] - 1) or #text
    local reg = text:sub(a, b)

    local id     = field(reg, "Id", true)
    local server = field(reg, "ServerId", true)
    local device = field(reg, "DeviceId", true)
    local itself = (device and server and device == server) or false

    if id and #id <= 32 and not itself then
      local who  = field(reg, "UserName", true) or ""
      local dev  = field(reg, "DeviceName", true) or ""
      local name = field(reg, "Name", true) or ""

      if who == "" then who = dev end
      if who == "" then who = "?" end

      out[#out + 1] = {
        id      = M.fit(id, 32),
        user    = M.fit(who, 24),
        device  = M.fit(dev, 24),
        title   = M.fit(name, 39),
        playing = name ~= "",
        paused  = reg:find('"IsPaused"%s*:%s*true') ~= nil,
      }

      detail[#out] = {
        line = lineFrom(reg),
        way  = M.fit(field(reg, "PlayMethod", true) or "", 20),
        at   = secsFrom(tonumber(field(reg, "PositionTicks")) or 0),
        len  = secsFrom(tonumber(field(reg, "RunTimeTicks")) or 0),
        can  = reg:find('"CanSeek"%s*:%s*true') ~= nil,

        -- THE SAME WORD AS THE EXACT ROAD (`M.nowPlaying()`), because the page
        -- reads one shape and not two: a session the server says is paused has
        -- a clock that stands still.
        paused = reg:find('"IsPaused"%s*:%s*true') ~= nil,

        -- THE SILENCE AND WHETHER THE CLIENT OBEYS THE ORDER, from the same
        -- text: `IsMuted` lives in `PlayState` (measured at byte 58 of the
        -- real body) and the three names are searched in the promise of the
        -- client (`SupportedCommands`, byte 361). Both are inside the 4 096
        -- bytes of a `GET`.
        muted   = reg:find('"IsMuted"%s*:%s*true') ~= nil,
        canMute = reg:find('"ToggleMute"', 1, true) ~= nil
                  or reg:find('"Unmute"', 1, true) ~= nil
                  or reg:find('"Mute"', 1, true) ~= nil,
        uid     = M.fit(field(reg, "UserId", true) or "", 32),

        -- THE ITEM'S OWN ID, WHICH IS THE ADDRESS OF THE PICTURE: it is the
        -- first `Id` that comes AFTER `"NowPlayingItem"` -the one before it is
        -- the session's own, measured at byte 959 of a body whose item starts
        -- at 1 212-. A cut that leaves the item out answers "" and the page is
        -- simply without a picture.
        itemId  = (function()
                    local np = reg:find('"NowPlayingItem"', 1, true)

                    if not np then return "" end

                    return M.fit(reg:match('"Id"%s*:%s*"([%w%-]+)"', np) or "", 32)
                  end)(),
      }
    end
  end

  return out, detail
end

--[[
`GET /Sessions`: who is connected, and what is playing.

Only the sessions the server says have an `Id` are kept -that is the handle a
command needs-, and the elements are read with `arrayPrefix()` because the
body may be cut: the last one, the one the cut went through, is not used and
the caller is told the list did not end.

AND WHEN NOT ONE OBJECT CAME WHOLE, THE SAME THING IS READ BY NAME (see
`scanSessions()`): a cut answer with the playing session's fields inside it is
a list of one, not an empty list.

Every text is cut to what a line of the page can hold: a name of forty bytes is
already wider than a row of this glass at the body size, and everything else is
cut by `fit()` where it is shown.
]]
function M.sessions(json, text)
  local list, complete = json.arrayPrefix(text)

  if type(list) ~= "table" or #list == 0 then
    -- AN EMPTY LIST IS AN ANSWER AND NOT A FAILURE: nobody is connected.
    if type(list) == "table" and complete == true then
      return {}, true, {}
    end

    -- AND WHEN NOT ONE OBJECT CAME WHOLE, THE SAME THING BY NAME. A cut body
    -- with a session's fields in it is a list of one, and it is the one that
    -- is playing: the road that works on the board today.
    local scanned, scannedDetail = scanSessions(text)

    if #scanned > 0 then
      return scanned, false, scannedDetail
    end

    --[[
    NOT BEING A LIST IS NOT ONE THING, AND SAYING WHICH ONE IS THE WHOLE
    DIAGNOSIS: an address that points at the web client of the server answers
    the page of the client with a 200 -Jellyfin serves it at `/` and for the
    routes it does not know-, and a JSON that is not a list is an object. The
    difference between "the address is wrong" and "the answer is too big" is
    the first character of what came back, and a person with the cable plugged
    in can read it in the log.

    MEASURED ON THE BOARD (20/10): the screen said "the list cannot be read"
    and there was no way to tell a wrong address from a web page. It is said
    now, in the line and in the log.
    ]]
    if text == "" then
      return nil, "empty: nothing came back"
    end

    local first = text:match("^%s*(%S)")

    if first == "<" then
      return nil, "a web page, not the list: check the address"
    end

    if first == "{" then
      return nil, "one object, not a list"
    end

    return nil, "not a list: '" .. tostring(first) .. "' first"
  end

  local out = {}
  local detail = {}

  for i = 1, #list do
    local s = list[i]

    if type(s) == "table" then
      local id = str(s.Id)

      -- THE SESSION EVERY REQUEST MAKES FOR ITSELF, AND WHY IT IS NOT A ROW.
      --
      -- Jellyfin logs each authenticated request as a session, and the device
      -- of an API key is the SERVER'S OWN id (`authInfo.DeviceId =
      -- _serverApplicationHost.SystemId`). Measured on the real server
      -- (20/10): after the first GET the list had one more row -"PaperOS on
      -- Jellyfin Server", no user, no item- which is this app looking at
      -- itself. It is not a client anybody can control, so it is not a row.
      local serverId = str(s.ServerId)
      local deviceId = str(s.DeviceId)
      local itself   = (deviceId ~= "" and serverId ~= "" and deviceId == serverId)

      -- 32 is what the server gives; a longer one is not a session id and a
      -- shorter one is a cut in the middle.
      if id ~= "" and #id <= 32 and not itself then
        local play   = type(s.PlayState) == "table" and s.PlayState or {}
        local item   = type(s.NowPlayingItem) == "table" and s.NowPlayingItem or nil
        local who    = str(s.UserName)
        local device = str(s.DeviceName)

        if who == "" then who = device end
        if who == "" then who = "?" end

        out[#out + 1] = {
          id      = M.fit(id, 32),
          user    = M.fit(who, 24),
          device  = M.fit(device, 24),
          title   = item and M.fit(str(item.Name), 39) or "",
          playing = item ~= nil,
          paused  = play.IsPaused == true,
        }

        -- THE SAME INDEX AS THE ROW, because the screen asks for the one of
        -- the session it has chosen. It is NOT part of the saved state: a
        -- table of a session may hold eight keys and this one has five more.
        detail[#out] = M.nowPlaying(item, play)

        detail[#out].canMute = M.canMute(s.SupportedCommands)

        -- WHO IS WATCHING: the resume list needs it and this is the only place
        -- the app can learn it with an API key.
        detail[#out].uid = str(s.UserId)

        -- AND WHAT IS PLAYING, by its own id, which is the address of the
        -- picture (`M.poster()`). A session that is not playing anything has
        -- no item and therefore no picture.
        detail[#out].itemId = item and str(item.Id) or ""
      end
    end
  end

  return out, complete == true, detail
end

-- The second line of a session's row: what it is doing, cut to the row.
function M.state(session)
  if not session.playing then return "Idle" end

  local head = session.paused and "Paused: " or "Playing: "

  return M.fit(head .. session.title, 39)
end

--[[
THE LIST OF WHAT IS HALF WATCHED, WHICH IS NOT AN ARRAY.

MEASURED (21/10): `/UserItems/Resume` answers an OBJECT with `Items` inside
-`{"Items":[…],"TotalRecordCount":n}`- and not a bare array, so the reader
finds `"Items":[` and hands the rest of the text to the same `arrayPrefix()`
the sessions use. A `GET` that came cut cuts that array, and the elements that
came whole are the ones that are kept.

A row carries the two lines it will need and nothing else: the title, the
percentage it has, the seconds that are left and the identity line the control
page already knows how to build (`M.nowPlaying()` with no play state).
]]
function M.resume(json, text)
  if type(text) ~= "string" or text == "" then
    return nil, "empty: nothing came back"
  end

  local at = text:find('"Items":[', 1, true)

  if not at then
    if text:find("<", 1, true) then
      return nil, "a web page, not the list: check the address"
    end

    return nil, "no Items in the answer"
  end

  -- FROM THE BRACKET ITSELF: `arrayPrefix()` wants the text to START with `[`,
  -- and `at + 8` is where that bracket is (`"Items":[` are nine bytes).
  local list, complete = json.arrayPrefix(text:sub(at + 8))
  local out = {}

  if type(list) ~= "table" then
    return nil, "Items is not a list"
  end

  for i = 1, #list do
    local it = list[i]

    if type(it) == "table" then
      local id = str(it.Id)

      if id ~= "" and #id <= 32 then
        local card = M.nowPlaying(it, nil)
        local pct  = 0

        if type(it.UserData) == "table" then
          pct = math.floor((tonumber(it.UserData.PlayedPercentage) or 0) + 0.5)
        end

        if pct < 0 then pct = 0 end
        if pct > 100 then pct = 100 end

        local left = 0

        if card.len > 0 then
          left = math.floor(card.len * (100 - pct) / 100 + 0.5)
        end

        out[#out + 1] = {
          id    = M.fit(id, 32),
          title = M.fit(str(it.Name), 39),
          pct   = pct,
          left  = left,
          line  = card.line,
        }
      end
    end
  end

  return out, complete == true
end

return M
