-- Page: downloads a page and prints its status and its body (docs/30).
--   run page
-- The address lives here and not in an argument, because `run` passes none: a
-- person changes this file, which is what an example is for.
--
-- The download brings the network up by itself with the network the board has
-- stored, and lets go of it when it is done. There is no `wifi connect` before it
-- and no `wifi disconnect` after it (docs/75).

local url = 'https://example.com/'

console.print('fetching ' .. url)
local ok, page = pcall(function() return http.get(url):await() end)
if not ok then
  console.print('no page: ' .. tostring(page and page.code))
  return
end

console.print('status ' .. page.status .. ' (' .. page.bytes .. ' bytes)')
console.print(page.body)
