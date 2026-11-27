-- ============================================================================
--  Jellyfin :: json.lua
-- ============================================================================
--  A SMALL JSON READER, ON ITS OWN BECAUSE IT HAS NO SCREEN AND NO RADIO:
--  `require('json')` returns this module, and the app only calls `decode()` or
--  `arrayPrefix()`.
--
--  WHY IT IS WRITTEN HERE AND NOT ASKED TO THE SYSTEM: the `json` table of this
--  system reads two fields out of a text (`get_string` and `get_int`) and is not
--  a parser, o sigui que una llista de sessions no en surt. El dia que dues apps
--  vulguin el mateix lector, el lector es farà una crida amb el seu contracte i
--  la seva prova; aquest fitxer és la mesura que diu que val la pena.
--
--  WHAT IT READS: objects, arrays, strings (with `\"`, `\\`, `\/`, `\b`, `\f`,
--  `\n`, `\r`, `\t` and `\uXXXX`), numbers, `true`, `false` and `null`. Nothing
--  else: no comments, no trailing commas, no single quotes.
--
--  AND IT KNOWS THAT THE ANSWER MAY BE CUT: `http.get` keeps **16 384 B** of a
--  body and a list of sessions is easily longer than that —and the one that is
--  asked through a file can come short for a reason of the card—, o sigui que
--  `arrayPrefix()` és la porta que diu «dona'm els elements que s'han pogut
--  llegir SENCERS i digues si la llista s'ha acabat».
-- ============================================================================

local M = {}

-- JSON's `null` is a value of its own and not `nil`: `nil` is the error of this
-- module, and a key whose value is `null` must not look like a key that is not
-- there. Whoever wants to know asks `== json.null`.
M.null = {}

-- Skips the white that JSON allows, and says where the next thing starts.
local function skip(text, i)
  local _, e = text:find("^[ \t\r\n]*", i)
  return (e or (i - 1)) + 1
end

local escapes = {
  ['"'] = '"',
  ["\\"] = "\\",
  ["/"] = "/",
  b = "\b",
  f = "\f",
  n = "\n",
  r = "\r",
  t = "\t",
}

--[[
A string, from its opening quote.

@return the text and the position after the closing quote, or nil and why
]]
local function parseString(text, i)
  if text:sub(i, i) ~= '"' then return nil, "a string was expected" end

  local out = {}
  local j   = i + 1

  while j <= #text do
    local c = text:sub(j, j)

    if c == '"' then return table.concat(out), j + 1 end

    if c == "\\" then
      local e = text:sub(j + 1, j + 1)

      if e == "u" then
        local hex  = text:sub(j + 2, j + 5)
        local code = tonumber(hex, 16)

        if not code then return nil, "a \\u escape without four hex digits" end

        -- A surrogate is half a character: the pair is not in a JSON this
        -- board will ever read, and writing the half would put invalid UTF-8
        -- on the glass.
        if code >= 0xD800 and code <= 0xDFFF then
          out[#out + 1] = ""
        else
          out[#out + 1] = utf8.char(code)
        end

        j = j + 6
      else
        out[#out + 1] = escapes[e] or e
        j = j + 2
      end
    else
      out[#out + 1] = c
      j = j + 1
    end
  end

  return nil, "the string was cut"
end

local decodeValue

decodeValue = function(text, i)
  i = skip(text, i)

  local c = text:sub(i, i)

  if c == "" then return nil, "there is nothing to read" end

  if c == "{" then
    local out = {}

    i = skip(text, i + 1)
    if text:sub(i, i) == "}" then return out, i + 1 end

    while true do
      local key
      key, i = parseString(text, i)
      if key == nil then return nil, "a key was expected" end

      i = skip(text, i)
      if text:sub(i, i) ~= ":" then return nil, "a colon was expected" end

      local value
      value, i = decodeValue(text, i + 1)
      if value == nil then return nil, i end

      out[key] = value

      i = skip(text, i)

      local d = text:sub(i, i)
      if d == "}" then return out, i + 1 end
      if d ~= "," then return nil, "a comma or a closing brace was expected" end

      -- **El blanc que ve després de la coma també es menja aquí** (0.199.1): the array below already did it and this loop
      -- did not, so an answer written with a newline or a space between two members —a server that pretty-prints, a person's
      -- saved answer— was a `a key was expected` in the middle of a good object. Jellyfin answers compact and the bug was
      -- never seen there; the port is not going to keep it.
      i = skip(text, i + 1)
    end
  end

  if c == "[" then
    local out = {}

    i = skip(text, i + 1)
    if text:sub(i, i) == "]" then return out, i + 1 end

    while true do
      local value
      value, i = decodeValue(text, i)
      if value == nil then return nil, i end

      out[#out + 1] = value

      i = skip(text, i)

      local d = text:sub(i, i)
      if d == "]" then return out, i + 1 end
      if d ~= "," then return nil, "a comma or a closing bracket was expected" end

      i = i + 1
    end
  end

  if c == '"' then return parseString(text, i) end

  if text:sub(i, i + 3) == "true" then return true, i + 4 end
  if text:sub(i, i + 4) == "false" then return false, i + 5 end
  if text:sub(i, i + 3) == "null" then return M.null, i + 4 end

  -- A number: the exponent is tried first because `%d+%.?%d*` alone would stop
  -- at the `e` and leave the rest to be read as another value.
  local s, e = text:find("^-?%d+%.?%d*[eE][-+]?%d+", i)

  if not s then s, e = text:find("^-?%d+%.?%d*", i) end
  if not s then return nil, "that is not a value" end

  return tonumber(text:sub(s, e)), e + 1
end

--[[
The first value of the text.

@return the value and the position after it, or nil and why
]]
function M.parse(text, from)
  if type(text) ~= "string" or text == "" then return nil, "there is no text" end
  return decodeValue(text, from or 1)
end

--[[
The whole text as one value, with nothing left over.

@return the value, or nil and why
]]
function M.decode(text)
  local value, i = M.parse(text, 1)

  if value == nil then return nil, i end

  if skip(text, i) <= #text then return nil, "there is something after the value" end

  return value
end

--[[
The elements of the top-level array that could be read WHOLE, and whether the
closing bracket was reached.

This is the door for a body that `NetJobs::MaxBody` may have cut: the last
element of a truncated answer has no closing brace and is never returned, so
the count is the count of things that are really there -the same rule the RSS
parser follows-.

@return the array and `true`, the array and `false` when it was cut, or nil
        and why when the text is not an array at all
]]
function M.arrayPrefix(text)
  local i = skip(text, 1)

  if text:sub(i, i) ~= "[" then return nil, "that is not an array" end

  local out = {}

  i = skip(text, i + 1)
  if text:sub(i, i) == "]" then return out, true end

  while true do
    local value, j = decodeValue(text, i)

    -- A value that cannot be read is the cut: what was whole is kept, and the
    -- caller is told that the list did not end.
    if value == nil then return out, false end

    out[#out + 1] = value

    i = skip(text, j)

    local c = text:sub(i, i)

    if c == "]" then return out, true end
    if c ~= "," then return out, false end

    i = skip(text, i + 1)
  end
end

return M
