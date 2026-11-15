local Controller = {
    mouseSensitivity = 0.2,
    previousMouse = Vector2.new(0, 0),
    dragging = false,
}

function Controller:OnDisable()
    self.dragging = false
end

function Controller:OnUpdate(deltaTime)
    if not Inputs.HasFocus() then
        self.dragging = false
        return
    end
    self:HandleMovement(deltaTime)
    local mouse = Inputs.GetMousePos()
    if not Inputs.GetMouseButtonDown(MouseButton.BUTTON_LEFT) then
        self.dragging = false
        return
    end
    if not self.dragging then
        self.previousMouse = mouse
        self.dragging = true
        return
    end
    local offset = mouse - self.previousMouse
    self.previousMouse = mouse
    if offset.x == 0 and offset.y == 0 then return end
    local transform = self.owner:GetTransform()
    local forward = transform:GetLocalForward()
    local pitch = math.asin(math.max(-1, math.min(1, -forward.y))) * 180 / math.pi
    local nextPitch = math.max(-89, math.min(89, pitch + offset.y * self.mouseSensitivity))
    local yaw = Quaternion.new(Vector3.new(0, -offset.x * self.mouseSensitivity, 0))
    local tilt = Quaternion.new(Vector3.new(nextPitch - pitch, 0, 0))
    transform:SetLocalRotation(Quaternion.Normalize(yaw * transform:GetLocalRotation() * tilt))
end

function Controller:HandleMovement(deltaTime)
    local transform = self.owner:GetTransform()
    local velocity = Vector3.new(0, 0, 0)
    local forward = transform:GetLocalForward()
    local right = transform:GetLocalRight()
    if Inputs.GetKeyDown(Key.A) then velocity = velocity + right end
    if Inputs.GetKeyDown(Key.D) then velocity = velocity - right end
    if Inputs.GetKeyDown(Key.W) then velocity = velocity + forward end
    if Inputs.GetKeyDown(Key.S) then velocity = velocity - forward end
    transform:SetLocalPosition(transform:GetLocalPosition() + velocity)
end

return Controller
