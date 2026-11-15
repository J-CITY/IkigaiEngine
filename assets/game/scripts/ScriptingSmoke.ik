var ticks = 0;
fun OnAwake() { Debug.Assert(owner.GetID() > 0, "Invalid owner ID"); Debug.Log("IkigaiScript OnAwake"); }
fun OnEnable() { owner.SetTag("enabled"); Debug.Log("IkigaiScript OnEnable"); }
fun OnStart() { Debug.Log("IkigaiScript OnStart"); }
fun OnUpdate(deltaTime) {
    ticks = ticks + 1;
    if (ticks == 1) {
        var leftDown = Inputs.GetMouseButtonDown(MouseButton.BUTTON_LEFT);
        var transform = owner.GetTransform();
        transform.SetLocalPosition(Vector3(1.0, 2.0, 3.0));
        Debug.Log("IkigaiScript OnUpdate " + owner.GetName());
    }
}
fun OnDisable() { owner.SetTag("disabled"); Debug.Log("IkigaiScript OnDisable"); }
fun OnDestroy() { owner.SetTag("destroyed"); Debug.Log("IkigaiScript OnDestroy"); }
