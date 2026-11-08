-- **The chapters of a book, in the order they are read** (docs/46, C2 and C14): the `META-INF/container.xml`, the OPF,
-- its `spine` and its index —the `nav` of EPUB 3 or the `toc.ncx` of EPUB 2—, read with `epub.read` and the patterns
-- this system's Lua has. A module of the reader since 0.149.0, and the biggest one that could leave the app without
-- sharing the app's state (docs/46, D6): it is given nothing and it takes nothing but the open book.
local M = {}

-- **The attributes of one tag of an XML file**: `id="c1" href="ch01.xhtml"` in a table. The OPF of a book is a
-- list of tags with attributes and nothing else this reader needs, so a pattern over a tag is the reader of the OPF
-- (docs/46, C2).
local function attributes(tag)
  local out = {}
  for key, value in tag:gmatch('([%w:%-]+)%s*=%s*"([^"]*)"') do out[key] = value end
  for key, value in tag:gmatch("([%w:%-]+)%s*=%s*'([^']*)'") do out[key] = value end
  return out
end

-- **A file of the book named inside another one**, with `..` and `.` taken out: the OPF lives in `OEBPS/` and its
-- `href`s are relative to that folder, while the zip knows nothing but the whole name (docs/46, C2).
local function join_path(folder, href)
  local parts = {}
  for piece in (folder .. href):gmatch('[^/]+') do
    if piece == '..' then
      if #parts > 0 then table.remove(parts) end
    elseif piece ~= '.' then
      parts[#parts + 1] = piece
    end
  end
  return table.concat(parts, '/')
end

-- **A label of the index as words** (docs/46, C14): the tags out, the entities a title uses decoded and the spaces
-- squeezed, because a `nav` of EPUB 3 writes its labels in XHTML and a `toc.ncx` writes plain text.
local function clean_label(text)
  text = text:gsub('<[^>]->', '')
  text = text:gsub('&nbsp;', ' '):gsub('&lt;', '<'):gsub('&gt;', '>'):gsub('&quot;', '"'):gsub('&amp;', '&')
  text = text:gsub('%s+', ' ')
  return text:gsub('^%s+', ''):gsub('%s+$', '')
end

-- **The ceiling of an index**: a book of a thousand chapters has an index of a thousand rows, and the list of the
-- reader is the one that pages itself (docs/46, C14).
local kMaxIndex = 128

-- **The chapters of the book, in the order they are read** (0.142.0, docs/46 C2): `META-INF/container.xml` names the
-- OPF, the OPF names every file of the book and its spine says the order — which is the order of the book and not the
-- order of the names. When the OPF cannot be read, the `.xhtml`, `.html` and `.htm` outside `META-INF`, sorted by name,
-- which is what the reader has done since the first day. It also says the title of the book and its index.
local function chapters_of_book()
  local known = {}
  for i = 1, epub.entries() do
    local name = epub.name(i)
    if name ~= nil then known[name] = true end
  end

  local spine = {}
  local title = nil
  local toc = {}
  local toc_from = 'none'
  local cover_href = nil
  -- **The faces the book carries** (0.150.0, docs/46 D6), in the order the manifest declares them: the first is the body
  -- and the second the titles, which is what a book with two faces means. The hrefs come out joined below, when the
  -- folder of the OPF is known.
  local fonts = {}

  local container = epub.read('META-INF/container.xml')
  local opf_path = container and (container:match('full%-path%s*=%s*"([^"]+)"') or
                                  container:match("full%-path%s*=%s*'([^']+)'"))
  if opf_path ~= nil then
    opf_path = opf_path:gsub('^/', '')
    local opf = epub.read(opf_path)
    if opf ~= nil then
      local said = opf:match('<dc:title[^>]*>([^<]*)</dc:title>') or opf:match('<title[^>]*>([^<]*)</title>')
      if said ~= nil and said ~= '' then title = said end

      local items = {}
      for tag in opf:gmatch('<item%s[^>]*>') do
        local a = attributes(tag)
        if a.id ~= nil and a.href ~= nil then items[a.id] = a end
        -- **A face of the book is an `item` a manifest knows** (0.150.0, docs/46 D6): the media type of EPUB 2 and of
        -- EPUB 3 for a font, or the extension of its file, which is what a book of the nineties writes.
        if a.href ~= nil then
          local kind = (a['media-type'] or ''):lower()
          local file = a.href:lower()
          if kind == 'application/vnd.ms-opentype' or kind == 'application/font-sfnt' or kind == 'font/ttf' or
             kind == 'font/otf' or kind == 'application/font-woff' or kind == 'font/woff' or kind == 'font/woff2' or
             file:match('%.ttf$') or file:match('%.otf$') or file:match('%.woff2?$') then
            fonts[#fonts + 1] = a.href
          end
        end
      end

      local base = opf_path:match('^(.*/)') or ''
      -- **And an href that is not in the book is not a face**: the manifest of a book lists files a reader may never
      -- see, and a name the zip does not carry is a `require` of nothing (docs/46, D6).
      local kept = {}
      for i = 1, #fonts do
        local href = join_path(base, fonts[i])
        if known[href] then kept[#kept + 1] = href end
      end
      fonts = kept
      for tag in opf:gmatch('<itemref%s[^>]*>') do
        local a = attributes(tag)
        local it = (a.idref ~= nil) and items[a.idref] or nil
        if it ~= nil then
          local href = join_path(base, it.href)
          if known[href] then spine[#spine + 1] = href end
        end
      end

      -- **The cover of the book** (0.144.0, docs/46 C7): EPUB 2 names it with `<meta name="cover" content="id">`,
      -- EPUB 3 marks the item with `properties="cover-image"`, and a book that has neither usually carries "cover" in
      -- the id or in the file name. It is only a cover if the entry is really in the book.
      if cover_href == nil then
        local meta = opf:match('<meta[^>]*name%s*=%s*"cover"[^>]*>')
        if meta ~= nil then
          local id = attributes(meta).content
          local it = (id ~= nil) and items[id] or nil
          if it ~= nil and it.href ~= nil then cover_href = join_path(base, it.href) end
        end
      end
      if cover_href == nil then
        for _, item in pairs(items) do
          if item.href ~= nil and item.properties ~= nil and item.properties:find('cover%-image') then
            cover_href = join_path(base, item.href)
            break
          end
        end
      end
      if cover_href == nil then
        for _, item in pairs(items) do
          local id = (item.id or ''):lower()
          local href = (item.href or ''):lower()
          local kind = (item['media-type'] or ''):lower()
          if kind:sub(1, 6) == 'image/' and (id:find('cover', 1, true) or href:find('cover', 1, true)) then
            cover_href = join_path(base, item.href)
            break
          end
        end
      end
      if cover_href ~= nil and not known[cover_href] then cover_href = nil end

      -- **The index the book declares** (0.143.0, docs/46 C14): EPUB 3 leaves a `nav` in an XHTML file of its own and
      -- EPUB 2 a `toc.ncx` with a `navPoint` per entry. The two end in a label and an href that is turned into a
      -- chapter of the spine, with its `#fragment` dropped — an entry lands on the first page of its chapter, which
      -- is what this reader can do.
      local nav_href, ncx_href = nil, nil
      for _, item in pairs(items) do
        local href = item.href or ''
        local kind = (item['media-type'] or ''):lower()
        if nav_href == nil and item.properties ~= nil and item.properties:find('nav', 1, true) then
          nav_href = join_path(base, href)
        end
        if ncx_href == nil and (kind == 'application/x-dtbncx+xml' or href:lower():match('%.ncx$')) then
          ncx_href = join_path(base, href)
        end
      end

      local function chapter_of(target)
        local file = join_path(base, (target:gsub('#.*$', '')))
        for i = 1, #spine do
          if spine[i] == file then return i end
        end
        return nil
      end

      local function add_entry(label, target)
        if #toc >= kMaxIndex then return end
        local at = chapter_of(target)
        if at == nil then return end
        -- **Two entries in the same file land on the same page**: the index keeps the first one, the chapter itself.
        if #toc > 0 and toc[#toc].at == at then return end
        local words = clean_label(label)
        if words == '' then return end
        toc[#toc + 1] = { title = words, chapter = spine[at], at = at }
      end

      if nav_href ~= nil and known[nav_href] then
        local src = epub.read(nav_href)
        if src ~= nil then
          local nav = src:match('<nav[^>]-toc.->(.-)</nav>') or src
          for href, label in nav:gmatch('<a%s[^>]-href%s*=%s*"([^"]+)"[^>]*>(.-)</a>') do
            add_entry(label, href)
          end
        end
        if #toc > 0 then toc_from = 'nav' end
      end

      if #toc == 0 and ncx_href ~= nil and known[ncx_href] then
        local src = epub.read(ncx_href)
        if src ~= nil then
          for label, target in
            src:gmatch('<text[^>]*>([^<]*)</text>%s*</navLabel>%s*<content%s[^>]-src%s*=%s*"([^"]+)"') do
            add_entry(label, target)
          end
        end
        if #toc > 0 then toc_from = 'ncx' end
      end
    end
  end

  if #spine == 0 then
    local out = {}
    for name in pairs(known) do
      if not name:find('^META%-INF/') and name:match('%.x?html?$') then out[#out + 1] = name end
    end
    table.sort(out)
    spine = out
  end

  -- **A book that declares no index gets one made of its chapters** (docs/46, C14): it is not the index the author
  -- wrote, it is the one that lets chapter thirty of eighty be reached. The name is the number and not the title —
  -- reading a title means inflating every chapter of the book when it is opened, and a book keeps one chapter in
  -- memory.
  if #toc == 0 and #spine > 1 then
    local most = (#spine < kMaxIndex) and #spine or kMaxIndex
    for k = 1, most do
      toc[k] = { title = 'Chapter ' .. k, chapter = spine[k], at = k }
    end
    toc_from = 'spine'
  end

  return spine, title, toc, toc_from, cover_href, fonts
end

M.chapters_of_book = chapters_of_book

return M
