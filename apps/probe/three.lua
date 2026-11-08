-- **A module the size of one of the reader's own** (0.151.12, temporary): what matters to the probe is that `require`
-- reads a Lua file of this app from the flash and compiles it, which is what the reader does three times when it
-- opens. The content is filler on purpose: none of these numbers is used by anything.
local M = {}

M.k000 = { name = 'entry 000', weight = 0, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k001 = { name = 'entry 001', weight = 7, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k002 = { name = 'entry 002', weight = 14, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k003 = { name = 'entry 003', weight = 21, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k004 = { name = 'entry 004', weight = 28, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k005 = { name = 'entry 005', weight = 35, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k006 = { name = 'entry 006', weight = 42, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k007 = { name = 'entry 007', weight = 49, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k008 = { name = 'entry 008', weight = 56, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k009 = { name = 'entry 009', weight = 63, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k010 = { name = 'entry 010', weight = 70, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k011 = { name = 'entry 011', weight = 77, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k012 = { name = 'entry 012', weight = 84, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k013 = { name = 'entry 013', weight = 91, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k014 = { name = 'entry 014', weight = 98, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k015 = { name = 'entry 015', weight = 105, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k016 = { name = 'entry 016', weight = 112, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k017 = { name = 'entry 017', weight = 119, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k018 = { name = 'entry 018', weight = 126, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k019 = { name = 'entry 019', weight = 133, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k020 = { name = 'entry 020', weight = 140, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k021 = { name = 'entry 021', weight = 147, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k022 = { name = 'entry 022', weight = 154, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k023 = { name = 'entry 023', weight = 161, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k024 = { name = 'entry 024', weight = 168, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k025 = { name = 'entry 025', weight = 175, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k026 = { name = 'entry 026', weight = 182, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k027 = { name = 'entry 027', weight = 189, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k028 = { name = 'entry 028', weight = 196, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k029 = { name = 'entry 029', weight = 203, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k030 = { name = 'entry 030', weight = 210, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k031 = { name = 'entry 031', weight = 217, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k032 = { name = 'entry 032', weight = 224, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k033 = { name = 'entry 033', weight = 231, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k034 = { name = 'entry 034', weight = 238, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k035 = { name = 'entry 035', weight = 245, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k036 = { name = 'entry 036', weight = 252, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k037 = { name = 'entry 037', weight = 259, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k038 = { name = 'entry 038', weight = 266, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k039 = { name = 'entry 039', weight = 273, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k040 = { name = 'entry 040', weight = 280, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k041 = { name = 'entry 041', weight = 287, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k042 = { name = 'entry 042', weight = 294, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k043 = { name = 'entry 043', weight = 301, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k044 = { name = 'entry 044', weight = 308, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k045 = { name = 'entry 045', weight = 315, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k046 = { name = 'entry 046', weight = 322, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }
M.k047 = { name = 'entry 047', weight = 329, more = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' }

return M
