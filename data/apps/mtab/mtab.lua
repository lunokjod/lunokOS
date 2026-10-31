-- Shows fs.mounts() and fs.space() by hand (docs/30, docs/70):
--   run mtab
-- The room is asked mount by mount, and it is the call that costs: on the board
-- each answer walks the file system, so three names of the same partition cost
-- three walks. /apps, /res and /data are the same disk as /littlefs and say so,
-- because it is the truth; /cfg is a door that may be the card (docs/70).

local mounts = fs.mounts()
console.print('mounts: ' .. #mounts)
for _, one in ipairs(mounts) do
  local room = 'unknown'
  local space, err = fs.space(one.path)
  if space then
    room = space.free .. ' of ' .. space.total .. ' B free'
  else
    room = 'unknown (' .. tostring(err and err.code) .. ')'
  end
  console.print('  ' .. one.path .. ' ' .. one.type .. ' '
    .. (one.writable and 'rw' or 'ro') .. '  ' .. room)
end
