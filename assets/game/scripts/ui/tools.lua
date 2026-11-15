local count = 0
local message = "Lua tools"

function GetCount() return count end
function SetCount(value) count = value end
function GetMessage() return message end
function SetMessage(value) message = value end
function CanMove() return Host.Get("camera.canMove") end
function Increment(args) count = count + args.amount end
function MoveUp(args) Host.Call("camera.move", {x = 0, y = 1, z = 0}) end
function Log(args) Host.Call("debug.log", {message = message}) end
