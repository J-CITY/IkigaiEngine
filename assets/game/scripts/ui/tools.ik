var count = 0;
var message = "IkigaiScript tools";

fun GetCount() { return count; }
fun SetCount(value) { count = value; }
fun GetMessage() { return message; }
fun SetMessage(value) { message = value; }
fun CanMove() { return Host.Get("camera.canMove"); }
fun Increment(args) { count = count + args["amount"]; }
fun MoveUp(args) {
    var values = dictionary();
    values["x"] = 0;
    values["y"] = 1;
    values["z"] = 0;
    Host.Call("camera.move", values);
}
fun Log(args) {
    var values = dictionary();
    values["message"] = message;
    Host.Call("debug.log", values);
}
