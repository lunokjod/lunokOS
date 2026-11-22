-- ============================================================================
--  lunokOS :: Password :: vault.lua
-- ============================================================================
--  THE FILE OF THE VAULT, AND NOTHING ELSE: no screen, no key, no colour.
--  It is `data/app/Password/vault.lua` of the paper project with the same format
--  and the same five fields, over the doors of this system (`docs/30`, `docs/45`).
--
--  WHAT IS ON THE DISK
--  -------------------
--
--      magic      5 bytes   "PWDB1"
--      version    1 byte    1
--      iterations 4 bytes  big-endian; the price of the derivation
--      salt      16 bytes  from the board's generator, once, when it was made
--      block     ...       `crypto.seal()`: nonce | ciphertext | tag
--
--  The block is the ONLY thing that holds a secret, and it is sealed with the
--  64 bytes `crypto.key()` derives from the master password. The header travels
--  in the clear on purpose: a file whose salt could not be read without the key
--  would need a second copy of the salt elsewhere, and the iteration count has
--  to be readable to be raised one day.
--
--  **A CHANGED HEADER IS NOT A LEAK, IT IS A REFUSAL**: the key is derived from
--  the salt and the count, so touching either one gives a different key and the
--  tag no longer matches. What an attacker can do is break the file —deny the
--  owner their own vault—, and that is exactly what a file they cannot read is.
--
--  WHAT THE PLAINTEXT IS
--  ---------------------
--
--      PWDBv1 \n  <count> \n
--      for each entry, for each of the five fields:
--          <length in decimal> \n <exactly that many bytes>
--
--  Every field is length-prefixed and nothing is escaped, so a note may hold a
--  newline, a tab or the word "PWDBv1" without breaking the file. The count is
--  read first and every length is checked against what is left: a vault that has
--  been cut in half is refused with its reason instead of being decoded into
--  nonsense.
--
--  WHERE IT GOES, AND WHY THE RULE IS THE PAPER'S
--  ---------------------------------------------
--
--  **On the memory card and nowhere else** (the owner, 24/10, in the paper
--  project: *«l'app ha d'escollir la memoria mes duradora del sistma per
--  guardar el vault, en aquest cas es l' SD sempre»*). Here the card is not a
--  unit an app picks: it is `/data` — **the ground of this app, which follows the
--  preferred disk** (`docs/70`: `cfg auto` is the card when there is one and the
--  flash when there is not) — so the vault lives in
--  `/data/password/passwords.pwdb` and **the app asks which memory that is**
--  before it writes a byte (`/data` answers `sd` or `littlefs` in
--  `fs.mounts()`). A vault that is not on the card still OPENS and reads,
--  read-only: the internal flash is the firmware and the apps and it is what gets
--  rewritten, and a vault of passwords that lives in one place or the other
--  depending on the day is a vault somebody leaves behind.
--
--  **AND IT IS SAVED THE SAFE WAY WITHOUT ASKING FOR IT**: `fs.write` writes the
--  whole file or leaves the old one untouched (`docs/70`), so the
--  `passwords.tmp` of the old project and the rename that followed it are the
--  kernel's work now and not this file's. A battery that dies in the middle
--  leaves the vault it had.
-- ============================================================================

local vault = {}

--- **The reason the system gave, in words** (`docs/30`: every door says no with a table `{code, message}`, and a file that
--- shows the table shows nothing a person can read). It is here and not only in the app because this module is the one that
--- asks the doors of the disk and of the arithmetic, and a reason that arrives in the middle of a save has to survive the
--- way back.
local function why_text(err)
  if type(err) == 'table' then return tostring(err.message or err.code or 'it could not be done') end
  if err == nil then return 'it could not be done' end

  return tostring(err)
end

vault.FileName = 'passwords.pwdb'

--- The memory a NEW vault may live in. Its name is the mount table's (`sd`) and
--- it is asked through `fs.mounts()` and not assumed: the day this board has
--- another memory, this is the one line that changes.
vault.Card = 'sd'

--- The file this run is about, when `Files` hands one over (`app.args.path`).
---
--- It becomes THE vault of this run —`path()` returns it— so the screen shows
--- what the person asked for and not the one of always. Nothing is created on it
--- and, because the prompt's authority is over that one path, it can be written
--- even when it is on the card (`docs/70`: the named path is writable).
vault.OpenPath = nil

vault.Magic = 'PWDB1'
vault.Version = 1

-- THE PRICE OF A DERIVATION. 10 000 iterations of PBKDF2-HMAC-SHA256 for 64
-- bytes is the paper project's number and it is stored in the file, so a future
-- version may raise it without orphaning what is already saved. What it costs on
-- this board is in the log of every unlock (`[CRYPTO] key: … ms`, docs/95).
vault.Iterations = 10000

vault.SaltLen = 16

-- 26 bytes of header plus 28 of nonce and tag, and a whole file of 16 KB is what
-- one write of this system takes: a vault that grows past it is a vault that
-- cannot be saved, and what a full vault says is a reason and not a crash.
vault.MaxFile = 16384
vault.MaxPlain = 16384 - 26 - 28

-- The five things an entry has, in the order the screen shows them.
vault.Fields = { 'title', 'user', 'pass', 'web', 'note' }
vault.FieldNames = { 'Title', 'User', 'Password', 'Web', 'Note' }

-- HOW LONG A FIELD MAY BE. It is the app's own number and not the keyboard's: a
-- password of 120 characters is already a lot to type on this glass, and the
-- field is one line.
vault.MaxField = 120

-- ============================================================== the number

--- A 32-bit number in four bytes, big-endian, without `string.pack`.
local function be32(n)
  n = math.floor(n)

  return string.char(n // 16777216 % 256, n // 65536 % 256, n // 256 % 256, n % 256)
end

local function from_be32(s)
  local a, b, c, d = s:byte(1, 4)

  return a * 16777216 + b * 65536 + c * 256 + d
end

-- =============================================================== the file

--- The ground of this app, which is the card when there is one (`docs/70`).
function vault.dir()
  local ok, dir = pcall(fs.data)

  if not ok or dir == nil then return '/data/password' end

  return dir
end

function vault.path()
  if vault.OpenPath ~= nil then return vault.OpenPath end

  return vault.dir() .. '/' .. vault.FileName
end

--- The folder the vault is in: the ground of the app, or the one of the file
--- `Files` handed over.
function vault.folder()
  local d = vault.path():match('^(.*)/[^/]+$')

  return d or vault.dir()
end

--- WHICH MEMORY IS THE GROUND OF THIS APP ON? `sd` is the card and `littlefs`
--- the internal flash, and it is the mount table that says it (`docs/30`).
function vault.ground()
  local ok, list = pcall(fs.mounts)

  if not ok or type(list) ~= 'table' then return nil end

  for i = 1, #list do
    if list[i].path == '/data' then return list[i].type end
  end

  return nil
end

--- IS THE CARD IN THE READER? Not the same question as "is it writable": a card
--- that is there is what a new vault needs, and the answer is the mount's name.
function vault.card()
  return vault.ground() == vault.Card
end

--- CAN THIS OPEN VAULT BE CHANGED? Only when it IS on the card, or when this run
--- was handed the file by somebody with the authority over that one path. An old
--- vault in the internal memory, or a card that a person has taken out, reads and
--- never writes.
function vault.editable(state)
  if state == nil then return false end
  if vault.OpenPath ~= nil then return true end

  return state.on_card == true and vault.card()
end

--- Is there a vault where this run looks? The temp is not: this system writes a
--- whole file or nothing (`docs/70`), so there is no half save to recover.
function vault.exists()
  local info = fs.stat(vault.path())

  return info ~= nil and info.type == 'file'
end

-- ========================================================== what is inside

--- The entries as the bytes that get sealed.
function vault.encode(entries)
  local out = { 'PWDBv1\n', tostring(#entries), '\n' }

  for i = 1, #entries do
    local e = entries[i]

    for f = 1, #vault.Fields do
      local value = e[vault.Fields[f]] or ''

      out[#out + 1] = tostring(#value)
      out[#out + 1] = '\n'
      out[#out + 1] = value
    end
  end

  return table.concat(out)
end

--- Reads `<length>\n<bytes>` and says where the next one starts.
local function take_field(text, at)
  local nl = text:find('\n', at, true)

  if nl == nil then return nil, 'a field of the vault has no length' end

  local digits = text:sub(at, nl - 1)

  if digits == '' or digits:find('%D') then
    return nil, 'a field of the vault has no length'
  end

  local len = tonumber(digits)

  if len == nil or len < 0 or nl + len > #text then
    return nil, 'a field of the vault is cut short'
  end

  return text:sub(nl + 1, nl + len), nl + len + 1
end

--- The entries, or nil and the reason the bytes are not a vault.
function vault.decode(text)
  local nl = text:find('\n', 1, true)

  if nl == nil or text:sub(1, nl - 1) ~= 'PWDBv1' then
    return nil, 'this is not a vault of this version'
  end

  local second = text:find('\n', nl + 1, true)

  if second == nil then return nil, 'the vault has no count' end

  local count = tonumber(text:sub(nl + 1, second - 1))

  if count == nil or count < 0 or count > 9999 then
    return nil, 'the vault has no count'
  end

  local entries = {}
  local at = second + 1

  for i = 1, count do
    local entry = {}

    for f = 1, #vault.Fields do
      local value, next_at = take_field(text, at)

      if value == nil then return nil, next_at end

      entry[vault.Fields[f]] = value
      at = next_at
    end

    entries[i] = entry
  end

  return entries
end

-- ================================================================ the key

--- Makes a vault: a new salt, a new key and nothing inside.
function vault.create(password)
  -- A NEW VAULT ONLY ON THE CARD, and the card is asked and not assumed.
  if not vault.card() then
    return nil, 'there is no memory card: a vault is kept on the card and nowhere else, '
                .. 'so put one in first'
  end

  if vault.OpenPath ~= nil then
    return nil, 'this screen opens a file, it does not make one'
  end

  if password == nil or password == '' then
    return nil, 'the master password is empty'
  end

  local salt, why = crypto.random(vault.SaltLen)

  if salt == nil then return nil, why_text(why) end

  local key, key_why = crypto.key(password, salt, vault.Iterations)

  if key == nil then return nil, why_text(key_why) end

  return {
    key = key,
    salt = salt,
    iterations = vault.Iterations,
    entries = {},
    on_card = true,
  }
end

--- Opens the vault that is on the disk with that master password.
function vault.unlock(password)
  local path = vault.path()

  if not vault.exists() then return nil, 'there is no vault yet' end

  local bytes, why = fs.read(path)

  if bytes == nil then return nil, why_text(why) end

  if #bytes < 26 then return nil, 'the vault file is too short' end

  if bytes:sub(1, 5) ~= vault.Magic then return nil, 'this file is not a vault' end

  local version = bytes:byte(6)

  if version ~= vault.Version then
    return nil, 'this vault was written by another version'
  end

  local iterations = from_be32(bytes:sub(7, 10))
  local salt = bytes:sub(11, 26)
  local blob = bytes:sub(27)

  if iterations < 1000 or iterations > 100000 then
    return nil, 'the vault does not say a price that can be paid'
  end

  local key, key_why = crypto.key(password, salt, iterations)

  if key == nil then return nil, why_text(key_why) end

  local plain, open_why = crypto.open(key, blob)

  if plain == nil then
    -- The system's reason says "the key or the file", which is the truth, but
    -- what a person needs to read is what they can do about it.
    return nil, 'wrong master password (or the file was changed): ' .. why_text(open_why)
  end

  local entries, decode_why = vault.decode(plain)

  if entries == nil then return nil, decode_why end

  return {
    key = key,
    salt = salt,
    iterations = iterations,
    entries = entries,
    -- **The memory this vault lives in is the one that decides what can be done
    -- in it**: the card's ground, or a file the prompt named —which the prompt's
    -- authority makes writable wherever it is— (docs/70).
    on_card = (vault.OpenPath == nil) and (vault.ground() == vault.Card),
  }
end

-- ================================================================ writing

--- Saves the whole vault. The temp of the old project is the kernel's now: a
--- `fs.write` is the new file or the old one, never half of each (docs/70).
function vault.save(state)
  if state == nil or state.key == nil then return nil, 'there is no open vault' end

  local plain = vault.encode(state.entries)

  if #plain > vault.MaxPlain then
    return nil, 'the vault is full: delete an entry'
  end

  local blob, seal_why = crypto.seal(state.key, plain)

  if blob == nil then return nil, why_text(seal_why) end

  local bytes = vault.Magic .. string.char(vault.Version) .. be32(state.iterations) .. state.salt .. blob

  if #bytes > vault.MaxFile then return nil, 'the sealed vault does not fit in one file' end

  -- AND IT IS ONLY WRITTEN WHERE IT MAY LIVE. A vault that was found in the
  -- internal memory —an old one, or one whose card is no longer in the reader—
  -- is a vault that READS: saving it would write it to the memory it must not
  -- live in, and that is two vaults and a person who does not know which one is
  -- theirs.
  if not vault.editable(state) then
    return nil, 'the vault is not on the memory card: it opens read-only'
  end

  local ok, why = fs.write(vault.path(), bytes)

  if ok == nil or ok == false then return nil, why_text(why) end

  return true
end

-- ================================================================ entries

--- The entry with the fields of `entry`, trimmed to what a field can hold.
function vault.normalize(entry)
  local out = {}

  for f = 1, #vault.Fields do
    local name = vault.Fields[f]
    local value = tostring(entry and entry[name] or '')

    if #value > vault.MaxField then value = value:sub(1, vault.MaxField) end

    out[name] = value
  end

  return out
end

--- True when the entry says nothing at all: a row of empty fields is not worth
--- saving and it would show as a blank line in the list.
function vault.is_empty(entry)
  for f = 1, #vault.Fields do
    if (entry[vault.Fields[f]] or '') ~= '' then return false end
  end

  return true
end

--- The title a list row shows: the title, or the web, or the user, or a name
--- for an entry that is only a password.
function vault.label(entry)
  if (entry.title or '') ~= '' then return entry.title end
  if (entry.web or '') ~= '' then return entry.web end
  if (entry.user or '') ~= '' then return entry.user end

  return '(no title)'
end

--- The second line of a row.
function vault.subtitle(entry)
  if (entry.user or '') ~= '' and (entry.web or '') ~= '' then
    return entry.user .. ' - ' .. entry.web
  end

  return entry.user or entry.web or ''
end

--- Entries in the order a person reads them: by title, ignoring case.
function vault.sort(entries)
  table.sort(entries, function(a, b)
    local x = vault.label(a):lower()
    local y = vault.label(b):lower()

    if x == y then return (a.user or '') < (b.user or '') end

    return x < y
  end)

  return entries
end

return vault
