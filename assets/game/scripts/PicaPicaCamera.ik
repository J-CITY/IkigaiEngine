// Hold RMB to fly. WASD move, Q/E descend/ascend, Shift accelerates.
var speed = 4.0;
var mouseSensitivity = 0.15;
var previousMouse = Vector2(0.0, 0.0);
var dragging = false;

fun OnDisable() { dragging = false; }

fun OnUpdate(deltaTime) {
    if (!Inputs.CanControlCamera()) { dragging = false; return; }
    if (!Inputs.GetMouseButtonDown(MouseButton.BUTTON_RIGHT)) { dragging = false; return; }
    var transform = owner.GetTransform();
    var mouse = Inputs.GetMousePos();
    if (!dragging) { previousMouse = mouse; dragging = true; return; }
    var offset = mouse - previousMouse;
    previousMouse = mouse;
    var forward = transform.GetLocalForward();
    var right = transform.GetLocalRight();
    var velocity = Vector3(0.0, 0.0, 0.0);
    if (Inputs.GetKeyDown(Key.W)) { velocity = velocity + forward; }
    if (Inputs.GetKeyDown(Key.S)) { velocity = velocity - forward; }
    if (Inputs.GetKeyDown(Key.A)) { velocity = velocity + right; }
    if (Inputs.GetKeyDown(Key.D)) { velocity = velocity - right; }
    if (Inputs.GetKeyDown(Key.Q)) { velocity.y = velocity.y - 1.0; }
    if (Inputs.GetKeyDown(Key.E)) { velocity.y = velocity.y + 1.0; }
    var step = speed * deltaTime;
    if (Inputs.GetKeyDown(Key.LEFT_SHIFT)) { step = step * 3.0; }
    transform.SetLocalPosition(transform.GetLocalPosition() + velocity * step);
    if (offset.x == 0.0 && offset.y == 0.0) { return; }
    var pitch = Math.AsinDegrees(-forward.y);
    var nextPitch = pitch + offset.y * mouseSensitivity;
    if (nextPitch > 89.0) { nextPitch = 89.0; }
    if (nextPitch < -89.0) { nextPitch = -89.0; }
    var yaw = Quaternion(Vector3(0.0, -offset.x * mouseSensitivity, 0.0));
    var tilt = Quaternion(Vector3(nextPitch - pitch, 0.0, 0.0));
    transform.SetLocalRotation(Quaternion.Normalize(Quaternion.Multiply(Quaternion.Multiply(yaw, transform.GetLocalRotation()), tilt)));
}
