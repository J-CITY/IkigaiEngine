local Lander = {}
local step = 1 / 120
local tank = 24 -- seconds of single-engine burn
local halfDeck = 64
local foot = 19
local sole = 22
local landingSpeed = 112  -- 2.24 m/s downwards
local landingDrift = 78
local landingTilt = math.rad(32)
local landingSpin = math.rad(80)
local crashSpeed = 155
-- Convex silhouette, in screen coordinates relative to the centre of mass.
local hull = {{0,-31}, {14,-8}, {22,12}, {foot,sole}, {-foot,sole}, {-22,12}, {-14,-8}}

-- The flight plane is 800 x 600 at the reference aspect, with 50 units per world metre.
local function position(actor, x, y)
    actor:GetTransform():SetLocalPosition(Vector3.new((x - 400) / 50, (300 - y) / 50, 0))
end

function Lander:layout()
    local size = Window.GetPixelSize()
    if size.x < 1 or size.y < 1 then return false end
    if self.width ~= size.x or self.height ~= size.y then
        self.width, self.height = size.x, size.y
        local scale = math.min(size.x / 800, size.y / 600)
        local marginX, marginY = (size.x / scale - 800) / 2, (size.y / scale - 600) / 2
        self.minX, self.maxX = -marginX, 800 + marginX
        local halfHeight = (300 + marginY) / math.cos(math.rad(7))
        self.minY, self.maxY = 300 - halfHeight, 300 + halfHeight
        self.board:GetTransform():SetLocalScale(Vector3.new(scale, scale, 1))
        self.board:GetTransform():SetLocalPosition(Vector3.new(marginX * scale, marginY * scale, 0))
        self.owner:GetCamera():SetSize(size.y / scale / 100)
    end
    return true
end

function Lander:reset()
    local side = math.random(0, 1)
    for i, pad in ipairs(self.pads) do
        pad.x = math.random(96, 290) + 414 * side
        pad.y = math.random(386, 535)
        position(pad.actor, pad.x, pad.y)
        side = 1 - side
    end
    self.x, self.y = self.pads[1].x, self.pads[1].y - sole
    self.vx, self.vy, self.angle, self.spin = 0, 0, 0, 0
    self.fuel, self.accumulator, self.delay = tank, 0, 0
    self.docked = true
    self.rocket:SetActive(true)
    self.success:SetActive(false)
    self.blastActor:GetParticleSystem3D():Stop(true)
    for _, jet in ipairs(self.jets) do
        jet.on = false
        jet.actor:GetParticleSystem3D():Stop(true)
    end
end

function Lander:initialize()
    local scene = Scenes.GetCurrentScene()
    local function actor(name) return assert(scene:FindActorByName(name), name) end
    self.board, self.rocket = actor("HUD"), actor("Rocket")
    self.gauge, self.success = actor("Fuel remaining"), actor("Landing confirmed")
    self.blastActor = actor("Impact")
    self.pads = {{actor = actor("Launch pad")}, {actor = actor("Landing pad")}}
    self.jets = {{actor = actor("Left exhaust")},
                 {actor = actor("Right exhaust")}}
    self:reset()
end

local function setJet(jet, on)
    if jet.on == on then return end
    jet.on = on
    if on then jet.actor:GetParticleSystem3D():Restart(math.random(1, 65535)) else jet.actor:GetParticleSystem3D():Stop(false) end
end

-- Separating axis test between the rotating hull and the solid deck.
local function contact(vertices, pad)
    local depth, normalX, normalY = math.huge, 0, 0
    local function separated(nx, ny)
        local centre = pad.x * nx + (pad.y + 7) * ny
        local radius = halfDeck * math.abs(nx) + 7 * math.abs(ny)
        local low, high = math.huge, -math.huge
        for _, p in ipairs(vertices) do
            local d = p[1] * nx + p[2] * ny
            low, high = math.min(low, d), math.max(high, d)
        end
        local towardsLow, towardsHigh = high - (centre - radius), centre + radius - low
        if towardsLow < 0 or towardsHigh < 0 then return true end
        local length = math.sqrt(nx * nx + ny * ny)
        local overlap = math.min(towardsLow, towardsHigh) / length
        if overlap < depth then
            local sign = towardsLow < towardsHigh and -1 or 1
            depth, normalX, normalY = overlap, sign * nx / length, sign * ny / length
        end
        return false
    end
    if separated(1, 0) or separated(0, 1) then return false end
    for i, p in ipairs(vertices) do
        local q = vertices[i % #vertices + 1]
        if separated(q[2] - p[2], p[1] - q[1]) then return false end
    end
    return depth, normalX, normalY
end

function Lander:advance(dt, left, right)
    local engines = left + right
    self.fuel = self.fuel - dt * engines
    if self.fuel <= 0 then self:reset() return true end
    if self.docked then
        if engines < 2 then return end
        self.docked = false
    end
    local previousBottom = self.y + math.max(sole * math.cos(self.angle) + foot * math.abs(math.sin(self.angle)),
        -31 * math.cos(self.angle))
    self.spin = (self.spin + (left - right) * 3.3 * dt) / (1 + 2.9 * dt)
    self.angle = (self.angle + self.spin * dt + math.pi) % (2 * math.pi) - math.pi
    local c, s = math.cos(self.angle), math.sin(self.angle)
    self.vx = (self.vx + engines * 106 * s * dt) / (1 + .22 * dt)
    self.vy = (self.vy + (132 - engines * 106 * c) * dt) / (1 + .13 * dt)
    self.x, self.y = self.x + self.vx * dt, self.y + self.vy * dt
    if self.x < self.minX - 31 or self.x > self.maxX + 31 or self.y < self.minY - 31 or self.y > self.maxY + 31 then
        self:reset()
        return true
    end

    local vertices = {}
    for i, p in ipairs(hull) do
        vertices[i] = {self.x + c * p[1] - s * p[2], self.y + s * p[1] + c * p[2]}
    end
    for i, pad in ipairs(self.pads) do
        local depth, nx, ny = contact(vertices, pad)
        if depth then
            -- Allow a little foot overhang when the centre of mass is still supported.
            local supported = math.abs(self.x - pad.x) <= halfDeck - foot / 2
            local gentle = previousBottom <= pad.y + 1 and self.vy >= 0 and self.vy <= landingSpeed
                and math.abs(self.vx) <= landingDrift and math.abs(self.angle) <= landingTilt
                and math.abs(self.spin) <= landingSpin and supported
            if gentle then
                self.x = math.max(pad.x - halfDeck + foot, math.min(pad.x + halfDeck - foot, self.x))
                self.angle, self.spin, self.vx, self.vy = 0, 0, 0, 0
                self.y, self.docked = pad.y - sole, true
                if i == 2 then
                    self.success:SetActive(true)
                    self.delay = .85
                end
            elseif math.max(0, -(self.vx * nx + self.vy * ny)) + math.abs(self.spin) * sole < crashSpeed then
                -- Push the hull clear and damp the incoming velocity along the contact normal.
                local incoming = math.min(0, self.vx * nx + self.vy * ny)
                self.x, self.y = self.x + nx * (depth + .05), self.y + ny * (depth + .05)
                self.vx, self.vy = (self.vx - 1.35 * incoming * nx) * .85, (self.vy - 1.35 * incoming * ny) * .85
                self.spin = self.spin * .65
                return
            else
                position(self.blastActor, self.x, self.y)
                self.rocket:SetActive(false)
                self.blastActor:GetParticleSystem3D():Restart(math.random(1, 65535))
                self.delay = .85
            end
            return true
        end
    end
end

function Lander:OnUpdate(dt)
    if not self.board then self:initialize() end
    if not self:layout() then return end
    local focus = Inputs.HasFocus()
    local left = focus and Inputs.GetKeyDown(Key.A)
    local right = focus and Inputs.GetKeyDown(Key.D)
    dt = math.min(dt, .1)
    local interrupted = false
    if self.delay > 0 then
        self.delay = self.delay - dt
        if self.delay <= 0 then self:reset() end
        interrupted = true
    else
        self.accumulator = self.accumulator + dt
        while self.accumulator >= step do
            self.accumulator = self.accumulator - step
            if self:advance(step, left and 1 or 0, right and 1 or 0) then
                self.accumulator, interrupted = 0, true
                break
            end
        end
    end
    setJet(self.jets[1], left and not interrupted)
    setJet(self.jets[2], right and not interrupted)
    position(self.rocket, self.x, self.y)
    self.rocket:GetTransform():SetLocalRotation(Quaternion.new(Vector3.new(0, 0, -math.deg(self.angle))))
    self.gauge:GetTransform():SetLocalScale(Vector3.new(self.fuel / tank, 1, 1))
end

return Lander
