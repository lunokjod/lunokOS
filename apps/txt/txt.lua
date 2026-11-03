-- Txt: the app that handles .txt files.
-- Reading files from Lua is still to come (docs/90, M4), so this one says what it
-- was handed: the path, the extension and where the request came from.
console.print("app " .. app.id)

if app.args then
  console.print("  file   " .. app.args.path)
  console.print("  ext    " .. app.args.ext)
  console.print("  source " .. app.args.source)
else
  console.print("  no file: start me with open <path>")
end
