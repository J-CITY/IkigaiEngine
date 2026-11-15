// One private runtime per IkigaiScriptComponent; owner is supplied by the engine.
var mouseSensitivity = 0.2;
var previousMouse = Vector2(0.0, 0.0);
var dragging = false;

fun OnDisable() { dragging = false; }

fun OnUpdate(deltaTime) {
    if (!Inputs.HasFocus()) { dragging = false; return; }
    var transform = owner.GetTransform();
    var velocity = Vector3(0.0, 0.0, 0.0);
    var forward = transform.GetLocalForward();
    var right = transform.GetLocalRight();
    if (Inputs.GetKeyDown(Key.A)) { velocity = velocity + right; }
    if (Inputs.GetKeyDown(Key.D)) { velocity = velocity - right; }
    if (Inputs.GetKeyDown(Key.W)) { velocity = velocity + forward; }
    if (Inputs.GetKeyDown(Key.S)) { velocity = velocity - forward; }
    transform.SetLocalPosition(transform.GetLocalPosition() + velocity);

    var mouse = Inputs.GetMousePos();
    if (!Inputs.GetMouseButtonDown(MouseButton.BUTTON_LEFT)) { dragging = false; return; }
    if (!dragging) { previousMouse = mouse; dragging = true; return; }
    var offset = mouse - previousMouse;
    previousMouse = mouse;
    if (offset.x == 0.0 && offset.y == 0.0) { return; }
    var pitch = Math.AsinDegrees(-forward.y);
    var nextPitch = pitch + offset.y * mouseSensitivity;
    if (nextPitch > 89.0) { nextPitch = 89.0; }
    if (nextPitch < -89.0) { nextPitch = -89.0; }
    var yaw = Quaternion(Vector3(0.0, -offset.x * mouseSensitivity, 0.0));
    var tilt = Quaternion(Vector3(nextPitch - pitch, 0.0, 0.0));
    transform.SetLocalRotation(Quaternion.Normalize(Quaternion.Multiply(Quaternion.Multiply(yaw, transform.GetLocalRotation()), tilt)));
}
