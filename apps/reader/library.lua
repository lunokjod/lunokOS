-- **The books of the shelf and where each one was left** (docs/46, C1 and C6): a module of the reader since 0.149.0.
--
-- **It is a module because it is a thing and not a piece of a page** (docs/46, D6): where the `.epub` files are is the
-- system's answer and not a walk of the flash, and where a person stopped reading is a file of the app's own data. What
-- the shelf asks is `shelf_of()` and what leaving a book asks is `remember(...)`; nothing of the page is here.
local M = {}

-- **The position of each book**, kept by the app: one line per book with the path, the chapter and the byte
-- (docs/46, C6). A file that cannot be written is a lost position and not a lost book.
-- **The positions live in the app's own data** and not beside the book: an app of this system writes in its own
-- folder and reads the rest of the machine, which is the rule that keeps one app out of another's papers
-- (docs/30, docs/70). The day the system gives an app a writable shelf, this line is the one that changes.
local kReading = (function()
  local ok, folder = pcall(fs.data)
  if ok and type(folder) == 'string' and folder ~= '' then return folder .. '/reading.txt' end
  return '/data/reading.txt'
end)()

local function positions()
  local out = {}
  local ok, data = pcall(fs.read, kReading)
  if not ok or type(data) ~= 'string' then return out end

  for line in data:gmatch('[^\n]+') do
    local path, chapter, byte, on_cover = line:match('^(.-)\t(.-)\t(%d+)\t?(%d*)$')
    if path ~= nil then
      out[path] = {chapter = chapter, byte = tonumber(byte) or 0, cover = (on_cover == '1')}
    end
  end

  return out
end

local function remember(path, chapter, byte, on_cover)
  local kept = positions()
  kept[path] = {chapter = chapter, byte = byte, cover = (on_cover == true)}

  local lines = {}
  for one, where in pairs(kept) do
    lines[#lines + 1] = string.format('%s\t%s\t%d\t%d', one, where.chapter, where.byte, where.cover and 1 or 0)
  end
  table.sort(lines)

  pcall(fs.write, kReading, table.concat(lines, '\n') .. '\n')
end

-- **The shelf is the reader's** and not the file system of the app (docs/46, C1): the folder of the books is the
-- system's, and an app that could walk the flash would be an app with a key to every room. `epub.books` answers the
-- paths and the app makes the title out of the name of the file.
local function shelf_of()
  local out = {}
  local ok, paths = pcall(epub.books)
  if not ok or type(paths) ~= 'table' then return out end

  for _, path in ipairs(paths) do
    local name = path:match('([^/]+)%.epub$')
    if name ~= nil then out[#out + 1] = { path = path, title = name } end
  end

  return out
end

-- **What this module is for** (0.149.0): three calls and no state. `require` hands the same table to every reader of it.
M.positions = positions
M.remember = remember
M.shelf_of = shelf_of

return M
