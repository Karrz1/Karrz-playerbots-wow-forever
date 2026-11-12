-- ForeverDots: countdown bars for your damage-over-time spells, beside your
-- character, grouped per enemy.
--
-- This client refuses enemy aura data to add-ons in combat, and add-ons cannot
-- read the combat log. So the bars are timed from your own casts instead:
-- when a damage-over-time spell of yours finishes casting on an enemy, a bar
-- starts with that spell's duration (read from its tooltip text), and a recast
-- on the same enemy restarts it. Nothing here touches aura data, so nothing
-- is refused and nothing is tainted.

local ADDON = "ForeverDots"
local ROW_H, HEAD_H, GAP = 18, 14, 2
local MAX_GROUPS = 8

local defaults = { locked = true, scale = 1.0, width = 200, red = 3, dur = {} }
local db
local root
local rows, heads = {}, {}
local groups = {}          -- list of { key, name, mark, isTarget, dots = { [spellName] = dot } }
local known = {}           -- spellID -> duration in seconds, or false when not a DoT
local slot = 0             -- counts target changes; used when GUIDs are hidden
local testMode = false
local dirty = true

local BAR_COLOR = { 0.58, 0.40, 0.85 }
local RED = { 1.0, 0.12, 0.12 }

local function pr(msg) DEFAULT_CHAT_FRAME:AddMessage("|cff9966ffForeverDots|r " .. msg) end

local errors = {}
local function Guard(fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok then
        err = tostring(err)
        if not errors[err] then
            errors[err] = true
            pr("error: " .. err)
        end
    end
end

local function Secret(v)
    return issecretvalue ~= nil and issecretvalue(v)
end

-- ---------------------------------------------------------------- spells

-- Duration of a damage-over-time spell from its tooltip, e.g. "over 18 sec"
-- or "every 3 sec for 30 sec". Returns false for anything else.
local function ReadDuration(spellID)
    local name = C_Spell.GetSpellName(spellID)
    if not name or Secret(name) then return nil end
    if db.dur[name:lower()] then return db.dur[name:lower()] end
    if C_Spell.IsSpellHarmful and not C_Spell.IsSpellHarmful(spellID) then return false end
    local text = C_Spell.GetSpellDescription(spellID)
    if not text or Secret(text) or text == "" then return nil end
    local lower = text:lower()
    if not (lower:find("damage") or lower:find("from the target")) then return false end
    local best
    for n in lower:gmatch("over (%d+%.?%d*) sec") do best = math.max(best or 0, tonumber(n)) end
    for n in lower:gmatch("for (%d+%.?%d*) sec") do best = math.max(best or 0, tonumber(n)) end
    for n in lower:gmatch("lasts (%d+%.?%d*) sec") do best = math.max(best or 0, tonumber(n)) end
    for n in lower:gmatch("over (%d+%.?%d*) min") do best = math.max(best or 0, tonumber(n) * 60) end
    if best and best >= 4 then return best end
    return false
end

local function Duration(spellID)
    local name = C_Spell.GetSpellName(spellID)
    if name and not Secret(name) and db.dur[name:lower()] then return db.dur[name:lower()] end
    if known[spellID] == nil then
        local ok, d = pcall(ReadDuration, spellID)
        if ok and d ~= nil then known[spellID] = d end
        if not ok then return nil end
    end
    return known[spellID]
end

-- Reading every spellbook spell once makes the client load the tooltip
-- texts, so the first cast of a spell already has its duration.
--
-- 1.2.0 read the whole spellbook in one go on PLAYER_LOGIN, on every
-- SPELLS_CHANGED and on every SPELL_TEXT_UPDATE. Reading the texts makes the
-- client send SPELL_TEXT_UPDATE for each one that arrives, so at login a few
-- hundred full scans ran back to back and client 70630 stopped it ("Script
-- from ForeverDots has exceeded its execution time limit", count 100, BRS
-- 2026-10-27). Now a scan is asked for at most once a second and reads
-- SCAN_STEP spellbook entries a frame; a text update only forgets that one
-- spell, which is read again at its next cast.
local SCAN_STEP = 15
local scan = { want = false, wantAt = 0, line = nil, idx = nil, last = nil }

local function RequestScan(delay)
    scan.want = true
    scan.wantAt = GetTime() + (delay or 1)
end

-- Reads up to SCAN_STEP entries; returns true while the scan is not finished.
local function ScanStep()
    if not (C_SpellBook and C_SpellBook.GetNumSpellBookSkillLines) then return false end
    local bank = Enum and Enum.SpellBookSpellBank and Enum.SpellBookSpellBank.Player or 0
    local lines = C_SpellBook.GetNumSpellBookSkillLines() or 0
    local budget = SCAN_STEP
    while scan.line <= lines do
        if not scan.idx then
            local li = C_SpellBook.GetSpellBookSkillLineInfo(scan.line)
            if li then
                local first = li.itemIndexOffset or 0
                scan.idx, scan.last = first + 1, first + (li.numSpellBookItems or 0)
            else
                scan.idx, scan.last = 1, 0
            end
        end
        while scan.idx <= scan.last do
            if budget <= 0 then return true end
            budget = budget - 1
            local info = C_SpellBook.GetSpellBookItemInfo(scan.idx, bank)
            scan.idx = scan.idx + 1
            if info and info.spellID and not info.isPassive then Duration(info.spellID) end
        end
        scan.line, scan.idx = scan.line + 1, nil
    end
    return false
end

-- Called from the frame tick: starts a requested scan once its delay is over
-- and runs a scan in progress one step further.
local function ScanTick()
    if scan.line then
        if not ScanStep() then scan.line, scan.idx = nil, nil end
        return
    end
    if scan.want and GetTime() >= scan.wantAt then
        scan.want = false
        scan.line, scan.idx = 1, nil
    end
end

-- ---------------------------------------------------------------- enemies

-- A key for the current target that can be compared: its GUID when the client
-- shows it, otherwise a number that changes every time the target changes.
local function TargetKey()
    local ok, g = pcall(UnitGUID, "target")
    if ok and not Secret(g) and g then return g end
    return "slot" .. slot
end

local function Hostile(unit)
    local ok, v = pcall(function()
        local h = UnitCanAttack("player", unit)
        if Secret(h) then return true end
        return h and true or false
    end)
    return not ok or v
end

local function FindGroup(key)
    for i, g in ipairs(groups) do
        if g.key == key then return g, i end
    end
end

local function OnCast(spellID)
    if Secret(spellID) then return end
    if not UnitExists("target") or not Hostile("target") then return end
    local d = Duration(spellID)
    if not d then return end
    local name = C_Spell.GetSpellName(spellID)
    local key = TargetKey()
    local g = FindGroup(key)
    if not g then
        g = { key = key, dots = {} }
        table.insert(groups, 1, g)
        while #groups > MAX_GROUPS do table.remove(groups) end
    end
    local okName, unitName = pcall(UnitName, "target")
    g.name = okName and unitName or nil
    local okMark, mark = pcall(GetRaidTargetIndex, "target")
    g.mark = (okMark and not Secret(mark)) and mark or nil
    g.dots[name] = {
        name = name,
        icon = C_Spell.GetSpellTexture(spellID),
        start = GetTime(),
        dur = d,
    }
    dirty = true
end

local function Prune()
    local now = GetTime()
    for i = #groups, 1, -1 do
        local g = groups[i]
        local any = false
        for n, dot in pairs(g.dots) do
            if now >= dot.start + dot.dur then g.dots[n] = nil dirty = true else any = true end
        end
        if not any then table.remove(groups, i) dirty = true end
    end
end

-- The current target died: its bars go.
local function TargetDied()
    local ok, dead = pcall(function()
        local d = UnitIsDead("target")
        if Secret(d) then return false end
        return d and true or false
    end)
    if ok and dead then
        local _, i = FindGroup(TargetKey())
        if i then table.remove(groups, i) dirty = true end
    end
end

-- ---------------------------------------------------------------- frames

local function NewRow()
    local r = CreateFrame("Frame", nil, root)
    r:SetHeight(ROW_H)
    r.icon = r:CreateTexture(nil, "ARTWORK")
    r.icon:SetSize(ROW_H, ROW_H)
    r.icon:SetPoint("LEFT", 0, 0)
    r.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    r.bar = CreateFrame("StatusBar", nil, r)
    r.bar:SetPoint("LEFT", r.icon, "RIGHT", 2, 0)
    r.bar:SetPoint("RIGHT", r, "RIGHT", 0, 0)
    r.bar:SetHeight(ROW_H)
    r.bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    r.bg = r.bar:CreateTexture(nil, "BACKGROUND")
    r.bg:SetAllPoints()
    r.bg:SetColorTexture(0, 0, 0, 0.5)
    r.name = r.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.name:SetPoint("LEFT", 4, 0)
    r.name:SetPoint("RIGHT", -34, 0)
    r.name:SetJustifyH("LEFT")
    r.time = r.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.time:SetPoint("RIGHT", -4, 0)
    r:Hide()
    return r
end

local function NewHead()
    local h = CreateFrame("Frame", nil, root)
    h:SetHeight(HEAD_H)
    h.mark = h:CreateTexture(nil, "ARTWORK")
    h.mark:SetSize(HEAD_H, HEAD_H)
    h.mark:SetPoint("LEFT", 0, 0)
    h.mark:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcons")
    h.name = h:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    h.name:SetPoint("LEFT", h.mark, "RIGHT", 2, 0)
    h.name:SetPoint("RIGHT", 0, 0)
    h.name:SetJustifyH("LEFT")
    h:Hide()
    return h
end

local nRow, nHead, y = 0, 0, 0

local function TakeHead()
    nHead = nHead + 1
    local h = heads[nHead]
    if not h then h = NewHead() heads[nHead] = h end
    h:ClearAllPoints()
    h:SetPoint("TOPLEFT", root, "TOPLEFT", 0, -y)
    h:SetPoint("RIGHT", root, "RIGHT", 0, 0)
    h:Show()
    y = y + HEAD_H + 1
    return h
end

local function TakeRow()
    nRow = nRow + 1
    local r = rows[nRow]
    if not r then r = NewRow() rows[nRow] = r end
    r:ClearAllPoints()
    r:SetPoint("TOPLEFT", root, "TOPLEFT", 0, -y)
    r:SetPoint("RIGHT", root, "RIGHT", 0, 0)
    r:Show()
    y = y + ROW_H + GAP
    return r
end

local function SampleGroups()
    local now = GetTime()
    local function dot(n, icon, d, left)
        return { name = n, icon = icon, dur = d, start = now - (d - left) }
    end
    return {
        { key = "a", name = "Sample target", isTarget = true, dots = {
            dot("Corruption", "Interface\\Icons\\Spell_Shadow_AbominationExplosion", 18, 14),
            dot("Immolate", "Interface\\Icons\\Spell_Fire_Immolation", 15, 9),
            dot("Curse of Agony", "Interface\\Icons\\Spell_Shadow_CurseOfSargeras", 24, 2.5) } },
        { key = "b", name = "Sample mob", dots = {
            dot("Corruption", "Interface\\Icons\\Spell_Shadow_AbominationExplosion", 18, 6),
            dot("Siphon Life", "Interface\\Icons\\Spell_Shadow_Requiem", 30, 22) } },
    }
end

local shownGroups

local function Layout()
    nRow, nHead, y = 0, 0, 0
    shownGroups = testMode and SampleGroups() or groups
    local targetKey = UnitExists("target") and TargetKey() or nil
    for _, g in ipairs(shownGroups) do
        local h = TakeHead()
        if not pcall(function() h.name:SetText(g.name) end) then h.name:SetText("?") end
        local isTarget = g.isTarget or (targetKey ~= nil and g.key == targetKey)
        if isTarget then h.name:SetTextColor(1, 0.82, 0) else h.name:SetTextColor(0.75, 0.75, 0.75) end
        h.mark:Hide()
        if g.mark then
            pcall(function() SetRaidTargetIconTexture(h.mark, g.mark) h.mark:Show() end)
        end
        local list = {}
        for _, dot in pairs(g.dots) do list[#list + 1] = dot end
        table.sort(list, function(a, b) return a.start + a.dur < b.start + b.dur end)
        for _, dot in ipairs(list) do
            local r = TakeRow()
            r.dot = dot
            r.icon:SetTexture(dot.icon)
            r.name:SetText(dot.name)
            r.bar:SetMinMaxValues(0, dot.dur)
        end
    end
    for i = nRow + 1, #rows do rows[i]:Hide() rows[i].dot = nil end
    for i = nHead + 1, #heads do heads[i]:Hide() end
    root:SetHeight(math.max(y, 16))
end

local function Tick()
    local now = GetTime()
    for i = 1, nRow do
        local r = rows[i]
        local dot = r.dot
        if dot then
            local left = math.max(dot.start + dot.dur - now, 0)
            r.bar:SetValue(left)
            if left <= db.red then
                r.bar:SetStatusBarColor(RED[1], RED[2], RED[3])
            else
                r.bar:SetStatusBarColor(BAR_COLOR[1], BAR_COLOR[2], BAR_COLOR[3])
            end
            if left >= 10 then
                r.time:SetText(string.format("%d", left))
            else
                r.time:SetText(string.format("%.1f", left))
            end
        end
    end
end

local function PlaceRoot()
    root:ClearAllPoints()
    if db.point then
        root:SetPoint(db.point, UIParent, db.relPoint or db.point, db.x or 0, db.y or 0)
    else
        -- just right of the middle of the screen, beside your character
        root:SetPoint("LEFT", UIParent, "CENTER", 90, -60)
    end
end

local function ApplyLock()
    if db.locked then
        root.handle:Hide() root.label:Hide() root:EnableMouse(false)
    else
        root.handle:Show() root.label:Show() root:EnableMouse(true)
    end
end

local function Build()
    root = CreateFrame("Frame", "ForeverDotsFrame", UIParent)
    root:SetSize(db.width, 16)
    root:SetMovable(true)
    root:SetClampedToScreen(true)
    root:RegisterForDrag("LeftButton")
    root:SetScale(db.scale)

    root.handle = root:CreateTexture(nil, "BACKGROUND")
    root.handle:SetAllPoints()
    root.handle:SetColorTexture(0.6, 0.4, 1, 0.25)
    root.label = root:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    root.label:SetPoint("BOTTOMLEFT", root, "TOPLEFT", 0, 2)
    root.label:SetText("ForeverDots (drag, then /dots lock)")

    root:SetScript("OnDragStart", function(self)
        if not db.locked then self:StartMoving() end
    end)
    root:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local p, _, rp, x, yy = self:GetPoint()
        db.point, db.relPoint, db.x, db.y = p, rp, x, yy
    end)

    PlaceRoot()
    ApplyLock()

    local acc = 0
    root:SetScript("OnUpdate", function(_, dt)
        acc = acc + dt
        if acc < 0.05 then return end
        acc = 0
        Guard(ScanTick)
        if not testMode then Guard(Prune) end
        if dirty then dirty = false Guard(Layout) end
        Guard(Tick)
    end)
end

-- ---------------------------------------------------------------- events

local ev = CreateFrame("Frame")
ev:RegisterEvent("ADDON_LOADED")
ev:RegisterEvent("PLAYER_LOGIN")
ev:RegisterEvent("PLAYER_TARGET_CHANGED")
ev:RegisterEvent("PLAYER_DEAD")
ev:RegisterEvent("SPELLS_CHANGED")
ev:RegisterEvent("SPELL_TEXT_UPDATE")
ev:RegisterUnitEvent("UNIT_SPELLCAST_SUCCEEDED", "player")
ev:RegisterUnitEvent("UNIT_HEALTH", "target")
ev:SetScript("OnEvent", function(_, event, arg1, arg2, arg3)
    if event == "ADDON_LOADED" then
        if arg1 ~= ADDON then return end
        ForeverDotsDB = ForeverDotsDB or {}
        db = ForeverDotsDB
        for k, v in pairs(defaults) do
            if db[k] == nil then db[k] = v end
        end
    elseif event == "PLAYER_LOGIN" then
        Guard(Build)
        RequestScan(2)
    elseif not root then
        return
    elseif event == "UNIT_SPELLCAST_SUCCEEDED" then
        Guard(OnCast, arg3)
    elseif event == "PLAYER_TARGET_CHANGED" then
        slot = slot + 1
        dirty = true
    elseif event == "UNIT_HEALTH" then
        Guard(TargetDied)
    elseif event == "PLAYER_DEAD" then
        wipe(groups)
        dirty = true
    elseif event == "SPELL_TEXT_UPDATE" then
        -- this spell's text arrived or changed: read it again at its next cast
        if arg1 and not Secret(arg1) then known[arg1] = nil end
    else
        -- SPELLS_CHANGED (new rank, talents): forget the durations, read the book again soon
        wipe(known)
        RequestScan(1)
    end
end)

-- ---------------------------------------------------------------- commands

SLASH_FOREVERDOTS1 = "/dots"
SLASH_FOREVERDOTS2 = "/foreverdots"
SlashCmdList["FOREVERDOTS"] = function(msg)
    if not root then return end
    msg = msg or ""
    local cmd, arg = msg:match("^(%S*)%s*(.-)$")
    cmd = cmd:lower()
    if cmd == "lock" then
        db.locked = true ApplyLock() pr("locked")
    elseif cmd == "unlock" then
        db.locked = false ApplyLock() pr("unlocked: drag the purple box, then /dots lock")
    elseif cmd == "reset" then
        db.point, db.relPoint, db.x, db.y = nil, nil, nil, nil
        PlaceRoot() pr("position reset")
    elseif cmd == "test" then
        testMode = not testMode
        pr(testMode and "showing sample bars (/dots test again to stop)" or "sample bars off")
        dirty = true
    elseif cmd == "clear" then
        wipe(groups) dirty = true pr("bars cleared")
    elseif cmd == "red" then
        local s = tonumber(arg)
        if s and s >= 0 and s <= 30 then db.red = s pr("bars turn red at " .. s .. " s left")
        else pr("usage: /dots red 0 to 30 (seconds)") end
    elseif cmd == "scale" then
        local s = tonumber(arg)
        if s and s >= 0.5 and s <= 2 then db.scale = s root:SetScale(s) pr("scale " .. s)
        else pr("usage: /dots scale 0.5 to 2") end
    elseif cmd == "width" then
        local w = tonumber(arg)
        if w and w >= 100 and w <= 400 then db.width = w root:SetWidth(w) pr("width " .. w)
        else pr("usage: /dots width 100 to 400") end
    elseif cmd == "time" then
        -- /dots time Corruption 18   (0 removes the override)
        local name, sec = arg:match("^(.-)%s+(%d+%.?%d*)$")
        if name and name ~= "" then
            sec = tonumber(sec)
            db.dur[name:lower()] = sec > 0 and sec or nil
            wipe(known)
            pr(name .. ": " .. (sec > 0 and (sec .. " s") or "back to the tooltip time"))
        else
            pr("usage: /dots time <spell name> <seconds>, e.g. /dots time Corruption 18")
        end
    elseif cmd == "spells" then
        local n = 0
        for id, d in pairs(known) do
            if d then n = n + 1 pr((C_Spell.GetSpellName(id) or id) .. ": " .. d .. " s") end
        end
        if n == 0 then pr("no damage-over-time spell cast yet this session") end
    else
        pr("/dots lock | unlock | reset | test | clear | red <sec> | scale <0.5-2> | width <100-400> | time <spell> <sec> | spells")
    end
end
