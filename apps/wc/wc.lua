-- Wc: the second app that handles .txt files.
-- It is here so that `open` has two apps to choose from for the same extension
-- and the selector shows up (docs/40). Counting the words needs to read the
-- file, and reading files from Lua is still to come (docs/90, M4), so it says
-- what it would count.
console.print("wc would count the words of " .. app.args.path)
