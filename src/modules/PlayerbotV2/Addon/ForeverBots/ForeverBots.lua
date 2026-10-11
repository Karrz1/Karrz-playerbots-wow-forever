--[[
ForeverBots: buttons for the server's .bot commands (Classic 1.60 client).

Everything goes through ordinary chat commands (".bot add priest healer", "follow", ...), so the addon needs no
addon-message protocol and keeps working whatever the server's addon channel does. The server answers in the chat
frame as it does for typed commands.

/fb (or /foreverbots) toggles the window. /fb add <class> [tank|healer|dps] adds one bot without opening it.
]]

local ADDON = ...
ForeverBotsDB = ForeverBotsDB or {}

-- This client hands tainted addon code "secret" strings for some units (an enemy NPC in combat) and some chat. A secret
-- value cannot be compared, concatenated or matched, so every name that comes from the game goes through SafeName / SafeStr
-- first and a secret one is treated as "not a bot": nothing in the menu or panel path touches it.
local function IsSecret(v)
    return issecretvalue ~= nil and issecretvalue(v)
end
local function SafeStr(v)
    if type(v) ~= "string" or IsSecret(v) then return nil end
    return v
end
local function SafeName(unit)
    local ok, n = pcall(UnitName, unit)
    if not ok then return nil end
    return SafeStr(n)
end

local CLASSES = {
    { key = "warrior", label = "Warrior", token = "WARRIOR", roles = { "tank", "dps" } },
    { key = "paladin", label = "Paladin", token = "PALADIN", roles = { "tank", "healer", "dps" }, faction = "Alliance" },
    { key = "shaman",  label = "Shaman",  token = "SHAMAN",  roles = { "healer", "dps" }, faction = "Horde" },
    { key = "hunter",  label = "Hunter",  token = "HUNTER",  roles = { "dps" } },
    { key = "rogue",   label = "Rogue",   token = "ROGUE",   roles = { "dps" } },
    { key = "priest",  label = "Priest",  token = "PRIEST",  roles = { "healer", "dps" } },
    { key = "mage",    label = "Mage",    token = "MAGE",    roles = { "dps" } },
    { key = "warlock", label = "Warlock", token = "WARLOCK", roles = { "dps" } },
    { key = "druid",   label = "Druid",   token = "DRUID",   roles = { "tank", "healer", "dps" } },
}

local ROLE_LABEL = { tank = "Tank", healer = "Heal", dps = "DPS" }

local function Print(msg)
    DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffKarrz's Player Bots|r: " .. msg)
end

-- Diagnostic output that nobody asked for (group roster, battleground hints) goes through Debug: silent unless "/fb debug on",
-- and never saved, so every login starts quiet.
local debugOn = false
local function Debug(msg)
    if debugOn then Print(msg) end
end

-- A dot command is handled by the server when it arrives as a normal say message.
local function ServerCommand(text)
    SendChatMessage(text, "SAY")
end

-- Stance toggle of the panel: false = the last order sent was passive (the default while you lead a dungeon group).
local stanceAggressive = false

-- Bot orders go to party (or raid) chat, where every bot of the group listens.
local function GroupOrder(text)
    if IsInRaid and IsInRaid() then
        SendChatMessage(text, "RAID")
    elseif (IsInGroup and IsInGroup()) or (GetNumPartyMembers and GetNumPartyMembers() > 0) then
        SendChatMessage(text, "PARTY")
    else
        Print("you have no group yet: add a bot first.")
    end
end

-- Raid markers the tank panel cycles through (the chat order words), and the one picked now.
local TANK_MARKERS = { "skull", "cross", "star", "circle", "diamond", "triangle", "moon", "square" }
local tankMarker = 1

-- Follow distance (yards, 1..20): the stepper in the panel and /fb distance <n>. The value is saved in ForeverBotsDB.distance
-- and sent to the group as the order "distance <n>". Clicks are coalesced: the final value goes out after 0.4 s without a
-- click and never more than one order per 0.3 s. It is re-sent silently on login and when the group changes size.
local DIST_MIN, DIST_MAX, DIST_START = 1, 20, 5
local InGroupQuiet
local distText                 -- value font string, filled by BuildFrame
local distDirtyAt              -- GetTime() of the last unsent change
local distLastSend = 0
local distSilentUntil = 0      -- hides the server's "Follow distance N yards" whisper after a silent re-send
local lastGroupSize = 0

local function Now() return GetTime and GetTime() or 0 end

local function ShowDistance()
    if distText then
        distText:SetText(ForeverBotsDB.distance and (ForeverBotsDB.distance .. " yd") or "auto")
    end
end

local function SendDistance(silent)
    local d = ForeverBotsDB.distance
    if not d then return end
    if not InGroupQuiet() then return end
    if silent then distSilentUntil = Now() + 3 end
    distLastSend = Now()
    distDirtyAt = nil
    GroupOrder("distance " .. d)
end

local function SetDistance(d)
    d = math.floor(tonumber(d) or DIST_START)
    d = math.max(DIST_MIN, math.min(DIST_MAX, d))
    ForeverBotsDB.distance = d
    ShowDistance()
    distDirtyAt = Now()
end

local function StepDistance(delta)
    SetDistance((ForeverBotsDB.distance or DIST_START) + delta)
end

-- Deletes the targeted companion for good, after a confirmation.
StaticPopupDialogs["FOREVERBOTS_DELETE"] = {
    text = "Delete %s for good? Its character is removed.",
    button1 = YES,
    button2 = NO,
    OnAccept = function(_, name) ServerCommand(".bot delete " .. name) end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

local function DeleteTarget()
    local name = UnitExists("target") and UnitIsPlayer("target") and SafeName("target")
    if not name or UnitIsUnit("target", "player") then
        Print("target the bot you want to delete first.")
        return
    end
    local dialog = StaticPopup_Show("FOREVERBOTS_DELETE", name)
    if dialog then dialog.data = name end
end

local function AddBot(class, role)
    if role then
        ServerCommand(".bot add " .. class .. " " .. role)
    else
        ServerCommand(".bot add " .. class)
    end
end

------------------------------------------------------------------------------------------------------------- assign
-- The client cannot put a raid icon on a friendly bot, so the panel assigns bots to icons through chat orders
-- ("assign <name> <marker> [tank|cc|kill]", "assign clear [marker|name]", "assign list"). The server keeps the
-- assignments; "assign list" makes ONE bot whisper "ASSIGN|<marker 0..7>|<name>|<role>" lines and a closing
-- "ASSIGN|END|<count>". Those whispers are parsed here and hidden from the chat frame.

local ICON_WORDS = { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" }   -- icon N = word N
local ICON_TEX = "Interface\\TargetingFrame\\UI-RaidTargetingIcon_"
local ROLE_CYCLE = { "auto", "tank", "cc", "kill" }
local roleChoice = 1
local assignments, pending = {}, {}
local printNextList = false
local listLines = {}          -- font strings of the panel list, filled by BuildFrame
local statusFS                -- one-line confirmation under the icon row, filled by BuildFrame
local lastAssign              -- { name, marker (1..8), role, at } of the click still waiting for the server

local function SetStatus(text)
    if statusFS then statusFS:SetText(text or "") end
end
local MAX_LIST = 8

InGroupQuiet = function()
    return (IsInRaid and IsInRaid()) or (IsInGroup and IsInGroup()) or (GetNumPartyMembers and GetNumPartyMembers() > 0)
end

local function RefreshList()
    for i, fs in ipairs(listLines) do
        local a = assignments[i]
        if a and i <= MAX_LIST then
            if i == MAX_LIST and #assignments > MAX_LIST then
                fs:SetText("+" .. (#assignments - MAX_LIST + 1) .. " more (/fb assign list)")
            else
                fs:SetText("|T" .. ICON_TEX .. (a.marker + 1) .. ":14|t  " .. a.name .. "  |cff9d9d9d" .. a.role .. "|r")
            end
        else
            fs:SetText(i == 1 and "|cff9d9d9dNo assignments.|r" or "")
        end
    end
end

-- Asks the server for the current assignments (one bot answers); the reply is parsed in the whisper handler.
-- The list request is a whisper to ONE group member (a bot answers a whisper by itself), never group chat: in raid or party chat
-- it reached all 40 bots, filled the server log and showed up in the raid chat.
local function ListAsker()
    local prefix, n = "party", (GetNumSubgroupMembers and GetNumSubgroupMembers()) or 0
    if IsInRaid and IsInRaid() then prefix, n = "raid", (GetNumGroupMembers and GetNumGroupMembers()) or 0 end
    for i = 1, n do
        local unit = prefix .. i
        if UnitExists(unit) and UnitIsPlayer(unit) and not UnitIsUnit(unit, "player") then
            local name = SafeName(unit)
            -- Names are not loaded yet right after a roster change: UnitName answers "Unknown" and the whisper would go nowhere.
            if name and name ~= UNKNOWNOBJECT and name ~= "Unknown" then return name end
        end
    end
    return nil
end

local function RequestList()
    -- no bot list inside a battleground/arena: bots there are not companions, and an addon-sent whisper is blocked in them
    -- (it was the "Interface action failed because of an AddOn" seen when bots join the instance group)
    local okI, inInstance, kind = pcall(IsInInstance)
    if okI and inInstance and (kind == "pvp" or kind == "arena") then return end
    if InGroupQuiet() then
        local asker = ListAsker()
        if asker then SendChatMessage("assign list", "WHISPER", nil, asker) end
    else
        assignments = {}
        RefreshList()
    end
end

local function RequestListSoon()
    if C_Timer and C_Timer.After then C_Timer.After(0.5, RequestList) else RequestList() end
end

local function AssignTarget(marker, button)   -- marker 1..8; right-click clears the icon
    if not (UnitExists("target") and UnitIsPlayer("target") and not UnitIsUnit("target", "player")
            and UnitIsFriend("player", "target")) then
        Print("target one of your bots first, then left-click an icon."); SetStatus("|cffff4040Target a bot first.|r")
        return
    end
    local name = SafeName("target")
    if not name then SetStatus("|cffff4040Target a bot first.|r"); return end
    if button == "RightButton" then
        lastAssign = nil
        GroupOrder("assign clear " .. ICON_WORDS[marker])
        SetStatus("|cffffff00Cleared " .. ICON_WORDS[marker] .. ", asking the server...|r")
    else
        local role = ROLE_CYCLE[roleChoice]
        GroupOrder("assign " .. strlower(name) .. " " .. ICON_WORDS[marker] .. (role ~= "auto" and (" " .. role) or ""))
        lastAssign = { name = name, marker = marker, role = role, at = GetTime and GetTime() or 0 }
        SetStatus("|cffffff00Sent: " .. name .. " on " .. ICON_WORDS[marker] .. ", waiting for the server...|r")
        Print("sent: " .. name .. " on " .. ICON_WORDS[marker] .. (role ~= "auto" and (" (" .. role .. ")") or "") .. ", waiting for the server to confirm.")
        roleChoice = 1
    end
    RequestListSoon()
end

local function ParseAssignLine(msg)
    local marker, name, role = strmatch(msg, "^ASSIGN|(%d)|([^|]+)|(%a+)$")
    if marker then
        pending[#pending + 1] = { marker = tonumber(marker), name = name, role = role }
        return
    end
    if strmatch(msg, "^ASSIGN|END|") then
        assignments, pending = pending, {}
        RefreshList()
        if lastAssign then
            local found
            for _, a in ipairs(assignments) do
                if a.marker + 1 == lastAssign.marker and strlower(a.name) == strlower(lastAssign.name) then found = a end
            end
            if found then
                SetStatus("|cff00ff00Confirmed: " .. found.name .. ": " .. found.role .. " on " .. ICON_WORDS[found.marker + 1] .. "|r")
                Print("confirmed: " .. found.name .. ": " .. found.role .. " on " .. ICON_WORDS[found.marker + 1])
                lastAssign = nil
            elseif (GetTime and GetTime() or 0) - lastAssign.at > 4 then
                SetStatus("|cffff4040The server did not confirm " .. lastAssign.name .. " on " .. ICON_WORDS[lastAssign.marker] .. ".|r")
                Print("the server did not confirm " .. lastAssign.name .. " on " .. ICON_WORDS[lastAssign.marker] .. " (is it your bot, in your group?).")
                lastAssign = nil
            end
        end
        if printNextList then
            printNextList = false
            if #assignments == 0 then Print("no assignments.") end
            for _, a in ipairs(assignments) do
                Print(ICON_WORDS[a.marker + 1] .. ": " .. a.name .. " (" .. a.role .. ")")
            end
        end
    end
end

local function IsAssignMsg(msg) return type(msg) == "string" and not IsSecret(msg) and strsub(msg, 1, 7) == "ASSIGN|" end

-- A per-bot confirmation whisper has the form "<BotName>: <what it did>" (server: BotCommandParser ConfirmWhisperOrder /
-- OrderAssign / OrderDistance). The bot panel below parses it into its status line and the chat frame does not show it.
local function ConfirmText(msg, author)
    if type(msg) ~= "string" or type(author) ~= "string" or IsSecret(msg) or IsSecret(author) then return nil end
    local who = strmatch(author, "^[^-]+")
    if not who then return nil end
    return strmatch(msg, "^" .. who .. ": (.+)$")
end

-- Hide the structured whispers and our own periodic "assign list" request from the chat frames.
local function WhisperFilter(_, _, msg, author)
    if IsAssignMsg(msg) then return true end
    -- /fb chat on: show the bot confirmations ("<Bot>: followed you") and distance replies in chat as well
    if ForeverBotsDB.showChat then return false end
    if ConfirmText(msg, author) then return true end
    return Now() < distSilentUntil and type(msg) == "string" and strmatch(msg, "^Follow distance %d+ yards$") ~= nil
end
local function OwnListFilter(_, _, msg, author)
    if msg ~= "assign list" and not (Now() < distSilentUntil and type(msg) == "string" and strmatch(msg, "^distance %d+$")) then
        return false
    end
    local me = SafeName("player")
    return me ~= nil and author ~= nil and strmatch(author, "^[^-]+") == me
end
if ChatFrame_AddMessageEventFilter then
    ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER", WhisperFilter)
    ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER_INFORM", function(_, _, msg) return type(msg) == "string" and not IsSecret(msg) and msg == "assign list" end)
    for _, ev in ipairs({ "CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER", "CHAT_MSG_RAID", "CHAT_MSG_RAID_LEADER" }) do
        ChatFrame_AddMessageEventFilter(ev, OwnListFilter)
    end
end

local whisperFrame = CreateFrame("Frame")
whisperFrame:RegisterEvent("CHAT_MSG_WHISPER")
whisperFrame:SetScript("OnEvent", function(_, _, msg)
    if IsSecret(msg) then
        if lastAssign then SetStatus("|cffff8000The server answered but the client hid the reply (restricted chat).|r") end
        return
    end
    if IsAssignMsg(msg) then ParseAssignLine(msg) end
end)


------------------------------------------------------------------------------------------------------------- window

local frame

local function MakeButton(parent, text, width, onClick, height)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    b:SetSize(width, height or 22)
    b:SetText(text)
    b:SetScript("OnClick", onClick)
    return b
end

local function ClassColor(token)
    local c = RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
    if c then
        return string.format("|cff%02x%02x%02x", c.r * 255, c.g * 255, c.b * 255)
    end
    return "|cffffffff"
end

-- Gear the next bots wear. The word goes to the group as the order "gear <word>" before an add; the server reads it
-- (match = like your own gear, the default; best = best for their level; bis; t1, t2, t3 = tier sets).
local GEARS = {
    { "match", "Match" }, { "best", "Best" }, { "bis", "BiS" },
    { "t1", "T1" }, { "t2", "T2" }, { "t3", "T3" },
}
local GEAR_NAME = {
    match = "Match my gear", best = "Best for their level", bis = "Best in slot",
    t1 = "Tier 1", t2 = "Tier 2", t3 = "Tier 3",
}
local gearSent = "match"           -- what the server holds for this group (its default until we say otherwise)

local function SendGear()
    local g = ForeverBotsDB.gear or "match"
    if not GEAR_NAME[g] then g = "match" end
    if InGroupQuiet() then
        if gearSent ~= g then
            gearSent = g
            GroupOrder("gear " .. g)
        end
        return true
    end
    return g == "match"
end

local function AddBotWithGear(class, role)
    local sent = SendGear()
    AddBot(class, role)
    if not sent and C_Timer and C_Timer.After then
        -- no group yet: the first bot makes one, so the gear word follows once it exists (it applies to the next bots)
        C_Timer.After(2.5, function() SendGear() end)
    end
end

local addPanel, gearChips, gearLabel

local function SetGearChoice(g)
    ForeverBotsDB.gear = g
    for key, chip in pairs(gearChips or {}) do
        if key == g then chip:Disable() else chip:Enable() end
    end
    if gearLabel then gearLabel:SetText("Now: |cffffd100" .. (GEAR_NAME[g] or g) .. "|r") end
    SendGear()
end

-- The add-bots drop-down sits beside the main window, on the right or, near the screen edge, on the left.
local function PlaceAddPanel()
    if not (addPanel and frame) then return end
    addPanel:ClearAllPoints()
    local right, screen = frame:GetRight(), UIParent:GetRight()
    if right and screen and right + addPanel:GetWidth() + 6 > screen then
        addPanel:SetPoint("TOPRIGHT", frame, "TOPLEFT", 2, -6)
    else
        addPanel:SetPoint("TOPLEFT", frame, "TOPRIGHT", -2, -6)
    end
end

local addToggleBtn
local function ShowAddPanel(show)
    ForeverBotsDB.addOpen = show and true or false
    if show then PlaceAddPanel(); addPanel:Show() else addPanel:Hide() end
    if addToggleBtn then addToggleBtn:SetText(show and "Add bots  <" or "Add bots  >") end
end

local function Tip(b, title, body)
    b:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(title)
        if body then GameTooltip:AddLine(body, 1, 1, 1, true) end
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

local function BuildFrame()
    frame = CreateFrame("Frame", "ForeverBotsFrame", UIParent, BackdropTemplateMixin and "BackdropTemplate" or nil)
    frame:SetSize(330, 10)
    frame:SetPoint("CENTER")
    frame:SetFrameStrata("DIALOG")
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local point, _, relPoint, x, y = self:GetPoint()
        ForeverBotsDB.pos = { point, relPoint, x, y }
        PlaceAddPanel()
    end)
    if frame.SetBackdrop then
        frame:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
            tile = true, tileSize = 32, edgeSize = 24,
            insets = { left = 6, right = 6, top = 6, bottom = 6 },
        })
    end
    if ForeverBotsDB.pos then
        local p = ForeverBotsDB.pos
        frame:ClearAllPoints()
        frame:SetPoint(p[1], UIParent, p[2], p[3], p[4])
    end

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOP", 0, -14)
    title:SetText("Karrz's Player Bots")

    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -4, -4)

    ---------------------------------------------------------------------------------------------- header: add + switches
    local y = -40
    addToggleBtn = MakeButton(frame, "Add bots  >", 100, function() ShowAddPanel(not addPanel:IsShown()) end, 24)
    addToggleBtn:SetPoint("TOPLEFT", 16, y)
    Tip(addToggleBtn, "Add bots", "Opens a list beside this window: choose the gear, then click a class and role. The new bot joins you at your level.")

    -- Two on/off switches. The icon shows the state last sent: coloured with a green ring = on, grey with a red ring = off.
    -- Area spells: the bots use their area spells whenever they can (server: "aoe_all on|off", whispered to one of your
    -- bots, which applies it to every bot in the group). Crowd control: on (the default) the bots crowd-control the mobs you
    -- mark, off they leave them alone (group order "crowdcontrol on|off").
    local sx = 128
    local function Switch(key, default, texture, caption, title, onText, offText, send)
        local b = CreateFrame("Button", nil, frame)
        b:SetSize(24, 24)
        b:SetPoint("TOPLEFT", sx, y)
        local tex = b:CreateTexture(nil, "ARTWORK")
        tex:SetAllPoints()
        tex:SetTexture(texture)
        local ring = b:CreateTexture(nil, "OVERLAY")
        ring:SetPoint("TOPLEFT", -2, 2)
        ring:SetPoint("BOTTOMRIGHT", 2, -2)
        ring:SetTexture("Interface\\Buttons\\UI-Quickslot-Depress")
        b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        local cap = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        cap:SetPoint("LEFT", b, "RIGHT", 4, 0)
        local function State()
            local on = ForeverBotsDB[key]
            if on == nil then on = default end
            return on
        end
        local function Paint()
            local on = State()
            if tex.SetDesaturated then tex:SetDesaturated(not on) end
            local c = on and 1 or 0.55
            tex:SetVertexColor(c, c, c)
            if on then ring:SetVertexColor(0.2, 1, 0.2) else ring:SetVertexColor(0.9, 0.2, 0.2) end
            cap:SetText(caption .. (on and " |cff00ff00on|r" or " |cffff4040off|r"))
        end
        Paint()
        b:SetScript("OnClick", function()
            ForeverBotsDB[key] = not State()
            Paint()
            send(State())
            if GameTooltip:IsOwned(b) then b:GetScript("OnEnter")(b) end
        end)
        b:SetScript("OnEnter", function(self)
            local on = State()
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(title .. ": " .. (on and "|cff00ff00ON|r" or "|cffff4040OFF|r"))
            GameTooltip:AddLine(on and onText or offText, 1, 1, 1, true)
            GameTooltip:AddLine("Click to switch.", 0.7, 0.7, 0.7)
            GameTooltip:Show()
        end)
        b:SetScript("OnLeave", function() GameTooltip:Hide() end)
        sx = sx + 94
    end
    Switch("aoe", false, "Interface\\Icons\\Spell_Shadow_RainOfFire", "AoE", "Area spells",
        "The bots use their area spells whenever they can.",
        "The bots use area spells only when several mobs are close together.",
        function(on)
            local asker = ListAsker()
            if asker then SendChatMessage("aoe_all " .. (on and "on" or "off"), "WHISPER", nil, asker)
            else Print("add a bot first: the area-spell setting goes to your group.") end
        end)
    Switch("cc", true, "Interface\\Icons\\Spell_Nature_Polymorph", "CC", "Crowd control",
        "The bots crowd-control the mobs you mark (give a bot and a mob the same icon, see Assign a bot to an icon).",
        "The bots leave marked mobs alone: no crowd control.",
        function(on) GroupOrder(on and "crowdcontrol on" or "crowdcontrol off") end)
    y = y - 34

    ---------------------------------------------------------------------------------------------- order rows
    local function Header(text, hint)
        local fs = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        fs:SetPoint("TOPLEFT", 16, y)
        fs:SetText(text)
        if hint then
            local h = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            h:SetPoint("TOPLEFT", 16, y - 15)
            h:SetText(hint)
            y = y - 15
        end
        y = y - 19
        return fs
    end
    -- one row of { label, width, tooltip title, tooltip text, function }; each button keeps its own width
    local function Row(defs)
        local x = 16
        local made = {}
        for _, d in ipairs(defs) do
            local b
            b = MakeButton(frame, d[1], d[2], function() d[5](b) end)
            b:SetPoint("TOPLEFT", x, y)
            if d[3] then Tip(b, d[3], d[4]) end
            made[#made + 1] = b
            x = x + d[2] + 3
        end
        y = y - 25
        return made
    end

    Header("Orders to the group", "Everything below goes to all your bots.")
    -- Move and fight.
    Row({
        { "Follow", 72, "Follow", "Bots follow you and only defend. They do not advance or pull until you say Attack, Pull or Resume.", function() GroupOrder("follow") end },
        { "Stay",   72, "Stay", "Bots hold where they stand.", function() GroupOrder("stay") end },
        { "Attack", 72, "Attack", "Bots attack your target.", function() GroupOrder("attack") end },
        { "Resume", 72, "Resume", "Hand the fight back: bots go on with their normal play.", function() GroupOrder("resume") end },
    })
    Row({
        { "Pull",      72, "Pull", "Your tank bots open on your target; the rest join once it is engaged.", function() GroupOrder("pull") end },
        { "Pull back", 72, "Pull back", "The main tank bot opens on your target from range, runs back to the rally point (or where it stood) and tanks it there, so nothing else aggros.", function() GroupOrder("pull back") end },
        { "Stop",      72, "Stop", "Bots stop attacking and go back to normal.", function() GroupOrder("disengage") end },
        { "Flee",      72, "Flee", "Bots break off and run to you.", function() GroupOrder("flee") end },
    })
    -- Stance toggle. Passive: the bots start nothing on their own and only defend themselves and the group until you say
    -- Attack or Pull. Aggressive: they attack anything hostile within about 10 yards of you. The button shows the stance
    -- it last sent (the bots start passive while you lead a dungeon group).
    Row({
        { "Come", 72, "Come", "Bots teleport to you.", function() GroupOrder("come") end },
        { stanceAggressive and "Aggressive" or "Passive", 98, "Stance",
          "Passive: bots start nothing and only defend. Aggressive: they attack anything hostile within about 10 yards of you. Shows what you last sent.",
          function(self)
              stanceAggressive = not stanceAggressive
              GroupOrder(stanceAggressive and "aggressive" or "passive")
              self:SetText(stanceAggressive and "Aggressive" or "Passive")
          end },
    })

    y = y - 8
    Header("After a wipe or to regroup")
    Row({
        -- Dead bots release their spirit and run back to their corpses; living ones ignore it.
        { "Release",   96, "Release", "After a wipe: every dead bot releases, runs back to its corpse and rejoins you. Living bots ignore it.", function() GroupOrder("release") end },
        -- Fall back: every bot drops the fight, runs to the rally point (set with Set rally, else where you stand) and
        -- re-forms there; it fights only what follows until Attack, Pull or Resume.
        { "Fall back", 96, "Fall back", "Every bot drops the fight and runs to the rally point, then re-forms there. It fights only what follows until you say Attack, Pull or Resume.", function() GroupOrder("fallback") end },
        { "Set rally", 96, "Set rally", "Sets the rally point to where you stand (used by Fall back and Pull back).", function() GroupOrder("rally here") end },
    })

    y = y - 8
    Header("Formation and spacing")
    Row({
        -- Formation round you out of combat (None = the plain follow behind you) and the spacing between bots.
        { "Circle",   72, "Circle", "Out of combat the bots stand in a ring round you.", function() GroupOrder("formation circle") end },
        { "Arrow",    72, "Arrow", "Out of combat the bots form an arrow pointing the way you face.", function() GroupOrder("formation arrow") end },
        { "Line",     72, "Line", "Out of combat the bots line up abreast behind you.", function() GroupOrder("formation line") end },
        { "No form.", 72, "No formation", "The plain follow behind you.", function() GroupOrder("formation none") end },
    })
    Row({
        { "Tight",  72, "Tight spacing", "About 2.5 yards between bots.", function() GroupOrder("spacing tight") end },
        { "Close",  72, "Close spacing", "About 4 yards between bots (the default).", function() GroupOrder("spacing close") end },
        { "Spread", 72, "Spread spacing", "About 8 yards between bots.", function() GroupOrder("spacing spread") end },
    })
    -- Distance row: how far the bots follow you (and the formation spacing), stepped 1 yard per click.
    local distLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    distLabel:SetPoint("TOPLEFT", 16, y - 4)
    distLabel:SetText("Follow distance")
    local distMinus = MakeButton(frame, "-", 26, function() StepDistance(-1) end)
    distMinus:SetPoint("TOPLEFT", 126, y)
    distText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    distText:SetPoint("TOPLEFT", 156, y - 5)
    distText:SetWidth(52)
    distText:SetJustifyH("CENTER")
    local distPlus = MakeButton(frame, "+", 26, function() StepDistance(1) end)
    distPlus:SetPoint("TOPLEFT", 212, y)
    Tip(distMinus, "Follow distance", "How far the bots follow you out of combat, 1 to 20 yards. Also sets the formation spacing.")
    Tip(distPlus, "Follow distance", "How far the bots follow you out of combat, 1 to 20 yards. Also sets the formation spacing.")
    ShowDistance()
    y = y - 30

    y = y - 4
    Header("Tanks and markers")
    Row({
        -- Sets the raid's Main Tank flag on your current target (a raid group member).
        { "Make main tank", 96, "Make main tank", "Sets the raid Main Tank flag on the raid member you target. Raid groups only.", function() GroupOrder("set main tank") end },
        -- Tank markers: the tank bot you target takes the mob carrying the chosen raid marker (the main tank's pull is
        -- marked skull and the off tanks' mobs the next markers on their own; this overrides that). The marker button
        -- cycles skull, cross, star, circle, diamond, triangle, moon, square.
        { "Marker: " .. TANK_MARKERS[tankMarker], 96, "Marker", "Click to cycle the marker (skull, cross, star, circle, diamond, triangle, moon, square) the next button hands out.", function(self)
            tankMarker = tankMarker % #TANK_MARKERS + 1
            self:SetText("Marker: " .. TANK_MARKERS[tankMarker])
        end },
        { "Give to tank", 96, "Give marker to tank", "The tank bot you target holds whichever mob carries this marker (mark the mob yourself with the raid icons).", function() GroupOrder("tank target " .. TANK_MARKERS[tankMarker]) end },
    })
    Row({
        { "Clear tanks", 96, "Clear tanks", "Back to the automatic tank split.", function() GroupOrder("clear tanks") end },
    })

    y = y - 8
    Header("Assign a bot to an icon", "Target a bot, left-click an icon. Right-click clears it.")
    statusFS = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    for i = 1, 8 do
        local ib = CreateFrame("Button", nil, frame)
        ib:SetSize(30, 30)
        ib:SetPoint("TOPLEFT", 16 + (i - 1) * 37, y)
        local tex = ib:CreateTexture(nil, "ARTWORK")
        tex:SetAllPoints()
        tex:SetTexture(ICON_TEX .. i)
        ib:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        ib:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        ib:SetScript("OnClick", function(_, button) AssignTarget(i, button) end)
        ib:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(ICON_WORDS[i])
            GameTooltip:AddLine("Click: assign the targeted bot", 1, 1, 1)
            GameTooltip:AddLine("Right-click: clear this icon", 1, 1, 1)
            GameTooltip:Show()
        end)
        ib:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end
    y = y - 34
    local roleBtn
    roleBtn = MakeButton(frame, "Role: auto", 98, function()
        roleChoice = roleChoice % #ROLE_CYCLE + 1
        roleBtn:SetText("Role: " .. ROLE_CYCLE[roleChoice])
    end)
    roleBtn:SetPoint("TOPLEFT", 16, y)
    Tip(roleBtn, "Role", "auto: a tank bot tanks the mob with that icon, casters crowd-control it until a skull goes on, the rest kill it first. Click to force tank, cc or kill for the next assignment.")
    local clearAll = MakeButton(frame, "Clear all", 98, function()
        GroupOrder("assign clear")
        lastAssign = nil
        SetStatus("|cffffff00Cleared all, asking the server...|r")
        RequestListSoon()
    end)
    clearAll:SetPoint("TOPLEFT", 117, y)
    local refreshBtn = MakeButton(frame, "Refresh", 98, RequestList)
    refreshBtn:SetPoint("TOPLEFT", 218, y)
    y = y - 26
    statusFS:SetPoint("TOPLEFT", 20, y)
    statusFS:SetJustifyH("LEFT")
    y = y - 16
    for i = 1, MAX_LIST do
        local fs = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        fs:SetPoint("TOPLEFT", 20, y)
        fs:SetJustifyH("LEFT")
        listLines[i] = fs
        if i < MAX_LIST then y = y - 15 end
    end
    RefreshList()
    frame:SetScript("OnUpdate", function(self, elapsed)
        self.fbTick = (self.fbTick or 0) + elapsed
        if self.fbTick >= 30 then self.fbTick = 0; RequestList() end
    end)

    y = y - 30
    Header("Your bots", "The bots you keep between sessions.")
    Row({
        { "Call saved",  98, "Call saved", "Brings your saved bots, and every bot in your group, to you.", function() ServerCommand(".summon saved") end },
        { "Save group",  98, "Save group", "Puts every bot with you on your saved list.", function() ServerCommand(".bot save all") end },
        { "Dismiss all", 98, "Dismiss all", "Sends all your bots away.", function() ServerCommand(".bot dismiss all") end },
    })
    Row({
        { "Unsave all",    98, "Unsave all", "Empties your saved list.", function() ServerCommand(".bot unsave all") end },
        { "Delete target", 98, "Delete target", "Deletes the targeted bot's character for good (asks first).", DeleteTarget },
        { "List",          98, "List", "Lists your bots in chat (saved ones are marked).", function() ServerCommand(".bot list") end },
    })
    Row({
        { "Regear all", 98, "Regear all", "Re-rolls the gear of your saved bots so they do not all wear the same pieces. Better pieces they already wear stay.", function() ServerCommand(".bot regear all") end },
    })

    frame:SetHeight(-y + 22)

    ---------------------------------------------------------------------------------------------- add-bots drop-down
    addPanel = CreateFrame("Frame", nil, frame, BackdropTemplateMixin and "BackdropTemplate" or nil)
    addPanel:SetSize(262, 10)
    addPanel:SetFrameStrata("DIALOG")
    addPanel:EnableMouse(true)
    if addPanel.SetBackdrop then
        addPanel:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
            tile = true, tileSize = 32, edgeSize = 16,
            insets = { left = 4, right = 4, top = 4, bottom = 4 },
        })
    end
    local gTitle = addPanel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    gTitle:SetPoint("TOPLEFT", 14, -14)
    gTitle:SetText("Select gear")
    gearChips = {}
    for i, g in ipairs(GEARS) do
        local chip = MakeButton(addPanel, g[2], 52, function() SetGearChoice(g[1]) end, 20)
        chip:SetPoint("TOPLEFT", 14 + ((i - 1) % 3) * 56, -32 - math.floor((i - 1) / 3) * 22)
        Tip(chip, GEAR_NAME[g[1]], "Gear for the bots you add next.")
        gearChips[g[1]] = chip
    end
    gearLabel = addPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    gearLabel:SetPoint("TOPLEFT", 14, -80)
    local aTitle = addPanel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    aTitle:SetPoint("TOPLEFT", 14, -102)
    aTitle:SetText("Add a bot")
    local faction = UnitFactionGroup("player")
    local ay = -120
    for _, class in ipairs(CLASSES) do
        if not class.faction or class.faction == faction then
            local name = addPanel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
            name:SetPoint("TOPLEFT", 14, ay - 4)
            name:SetText(ClassColor(class.token) .. class.label .. "|r")
            local bx = 78
            for _, role in ipairs(class.roles) do
                local b = MakeButton(addPanel, ROLE_LABEL[role], 54, function() AddBotWithGear(class.key, role) end)
                b:SetPoint("TOPLEFT", bx, ay)
                bx = bx + 57
            end
            ay = ay - 24
        end
    end
    local aHint = addPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    aHint:SetPoint("TOPLEFT", 14, ay - 4)
    aHint:SetText("A new bot joins you at your level.")
    addPanel:SetHeight(-ay + 28)
    addPanel:Hide()
    SetGearChoice(GEAR_NAME[ForeverBotsDB.gear or ""] and ForeverBotsDB.gear or "match")

    frame:Hide()
    -- Deliberately NOT in UISpecialFrames: Escape and "close all windows" must not close the panel.
    -- Only /fb (or the X button) closes it, and the open state is remembered across /reload and relog.
    frame:SetScript("OnShow", function(self)
        ForeverBotsDB.open = true
        self.fbTick = 0
        RequestList()
        ShowAddPanel(ForeverBotsDB.addOpen)
    end)
    frame:SetScript("OnHide", function() ForeverBotsDB.open = false end)
end


-- Sends the coalesced distance order and re-sends it silently when the group grows or shrinks (new bots need it).
local distFrame = CreateFrame("Frame")
distFrame.acc = 0
distFrame:SetScript("OnUpdate", function(self, elapsed)
    self.acc = self.acc + elapsed
    if self.acc < 0.1 then return end
    self.acc = 0
    local now = Now()
    if distDirtyAt and now - distDirtyAt >= 0.4 and now - distLastSend >= 0.3 then
        SendDistance(false)
    end
    if self.groupAt and now >= self.groupAt then
        self.groupAt = nil
        SendDistance(true)
    end
end)
local function GroupSize()
    if GetNumGroupMembers then return GetNumGroupMembers() end
    if GetNumRaidMembers and GetNumRaidMembers() > 0 then return GetNumRaidMembers() end
    return (GetNumPartyMembers and GetNumPartyMembers() or 0)
end
local function OnGroupChanged()
    local n = GroupSize()
    if n ~= lastGroupSize then
        lastGroupSize = n
        if n == 0 then gearSent = "match" end
        if n > 0 and ForeverBotsDB.distance then distFrame.groupAt = Now() + 2 end
    end
end
for _, ev in ipairs({ "GROUP_ROSTER_UPDATE", "PARTY_MEMBERS_CHANGED", "RAID_ROSTER_UPDATE" }) do
    pcall(distFrame.RegisterEvent, distFrame, ev)
end
distFrame:SetScript("OnEvent", OnGroupChanged)

local function Toggle()
    if not frame then BuildFrame() end
    if frame:IsShown() then frame:Hide() else frame:Show() end
end

------------------------------------------------------------------------------------------------------------- bot panel
-- A small floating panel for ONE bot: it follows your target whenever that is a friendly player in your group (a bot),
-- and every button is a whisper to that bot ("/w <name> follow"), so the order acts on that bot only and the bot
-- answers with a confirmation whisper "<Name>: <what it did>", which fills the status line (and is hidden from chat).
-- Not a protected frame, so it opens and closes in combat. /fb bot on|off toggles it (default on); the position is saved
-- in ForeverBotsDB.botPanelPos. Another bot's whisper that is not a confirmation stays in chat and also lands in the status.
-- Bot detection: the client has no "is a bot" flag, so a bot is a friendly player of your party/raid other than you. Names
-- that have answered with a confirmation are remembered for the session (knownBots) and shown without the "?" mark.

local botPanel, botName, botClassToken, botStatusFS, botTitleFS, botDistFS, botRoleBtn
local botRoleChoice = 1                 -- ROLE_CYCLE index for the next icon click (resets to auto)
local botDist = nil                     -- yards shown for the current bot
local botDistDirtyAt, botSentAt, botForced
local knownBots = {}

local function BotPanelEnabled() return ForeverBotsDB.botPanel ~= false end

local function SetBotStatus(text) if botStatusFS then botStatusFS:SetText(text or "") end end

local function InMyGroup(unit)
    if not (UnitExists(unit) and UnitIsPlayer(unit)) or UnitIsUnit(unit, "player") or not UnitIsFriend("player", unit) then
        return false
    end
    return (UnitInParty and UnitInParty(unit)) or (UnitInRaid and UnitInRaid(unit)) and true or false
end

local function BotWhisper(text)
    if not botName then return end
    SendChatMessage(text, "WHISPER", nil, botName)
    botSentAt = Now()
    SetBotStatus("|cffffff00" .. text .. " ...|r")
end

local function BotStepDistance(delta)
    botDist = math.max(DIST_MIN, math.min(DIST_MAX, math.floor((botDist or ForeverBotsDB.distance or DIST_START) + delta)))
    if botDistFS then botDistFS:SetText(botDist .. " yd") end
    botDistDirtyAt = Now()
end

local function UpdateBotPanel()
    if not botPanel then return end
    local name, token
    if botForced then
        name, token = botForced.name, botForced.token
    elseif BotPanelEnabled() and InMyGroup("target") then
        name = SafeName("target")
        token = select(2, UnitClass("target"))
    end
    if not name then
        botName = nil
        botPanel:Hide()
        return
    end
    if name ~= botName then
        botName, botClassToken = name, token
        botRoleChoice = 1
        botRoleBtn:SetText("Role: auto")
        botDist = ForeverBotsDB.distance or DIST_START
        botDistFS:SetText(botDist .. " yd")
        botDistDirtyAt, botSentAt = nil, nil
        SetBotStatus("")
    end
    botTitleFS:SetText(ClassColor(botClassToken) .. name .. "|r" .. (knownBots[name] and "" or " |cff9d9d9d?|r"))
    botPanel:Show()
end

local function BuildBotPanel()
    local p = CreateFrame("Frame", "ForeverBotsBot", UIParent, BackdropTemplateMixin and "BackdropTemplate" or nil)
    botPanel = p
    p:SetSize(236, 140)
    p:SetFrameStrata("MEDIUM")
    p:SetClampedToScreen(true)
    p:SetMovable(true)
    p:EnableMouse(true)
    p:RegisterForDrag("LeftButton")
    p:SetScript("OnDragStart", p.StartMoving)
    p:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local point, _, relPoint, x, y = self:GetPoint()
        ForeverBotsDB.botPanelPos = { point, relPoint, x, y }
    end)
    if p.SetBackdrop then
        p:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
            tile = true, tileSize = 32, edgeSize = 16,
            insets = { left = 4, right = 4, top = 4, bottom = 4 },
        })
    end
    local pos = ForeverBotsDB.botPanelPos
    if pos then
        p:SetPoint(pos[1], UIParent, pos[2], pos[3], pos[4])
    elseif TargetFrame then
        p:SetPoint("TOPLEFT", TargetFrame, "BOTTOMLEFT", 0, -4)
    else
        p:SetPoint("CENTER", UIParent, "CENTER", 200, 0)
    end

    botTitleFS = p:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    botTitleFS:SetPoint("TOPLEFT", 12, -10)
    local close = CreateFrame("Button", nil, p, "UIPanelCloseButton")
    close:SetSize(22, 22)
    close:SetPoint("TOPRIGHT", -2, -2)
    close:SetScript("OnClick", function() botForced = nil; botPanel:Hide() end)   -- until the next target change

    for i = 1, 8 do
        local ib = CreateFrame("Button", nil, p)
        ib:SetSize(24, 24)
        ib:SetPoint("TOPLEFT", 12 + (i - 1) * 27, -30)
        local tex = ib:CreateTexture(nil, "ARTWORK")
        tex:SetAllPoints()
        tex:SetTexture(ICON_TEX .. i)
        ib:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        ib:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        ib:SetScript("OnClick", function(_, button)
            if not botName then return end
            if button == "RightButton" then
                BotWhisper("assign clear " .. ICON_WORDS[i])
            else
                local role = ROLE_CYCLE[botRoleChoice]
                BotWhisper("assign " .. ICON_WORDS[i] .. (role ~= "auto" and (" " .. role) or ""))
                botRoleChoice = 1
                botRoleBtn:SetText("Role: auto")
            end
        end)
        ib:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(ICON_WORDS[i])
            GameTooltip:AddLine("Click: this bot takes this icon", 1, 1, 1)
            GameTooltip:AddLine("Right-click: clear it", 1, 1, 1)
            GameTooltip:Show()
        end)
        ib:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end

    local function B(text, w, x, y, fn)
        local b = MakeButton(p, text, w, fn)
        b:SetPoint("TOPLEFT", x, y)
        return b
    end
    B("Follow", 72, 12, -58, function() BotWhisper("follow") end)
    B("Stay", 72, 87, -58, function() BotWhisper("stay") end)
    B("Attack", 72, 162, -58, function() BotWhisper("attack") end)
    B("Pull back", 72, 12, -84, function() BotWhisper("pull back") end)
    B("-", 24, 92, -84, function() BotStepDistance(-1) end)
    botDistFS = p:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    botDistFS:SetPoint("TOPLEFT", 118, -89)
    botDistFS:SetWidth(44)
    botDistFS:SetJustifyH("CENTER")
    B("+", 24, 166, -84, function() BotStepDistance(1) end)
    botRoleBtn = B("Role: auto", 84, 12, -110, function()
        botRoleChoice = botRoleChoice % #ROLE_CYCLE + 1
        botRoleBtn:SetText("Role: " .. ROLE_CYCLE[botRoleChoice])
    end)
    B("Dismiss", 72, 102, -110, function() if botName then ServerCommand(".bot dismiss " .. botName) end end)
    local botStanceAggressive = false
    local botStanceBtn
    botStanceBtn = B("Passive", 56, 177, -110, function()
        botStanceAggressive = not botStanceAggressive
        BotWhisper(botStanceAggressive and "aggressive" or "passive")
        botStanceBtn:SetText(botStanceAggressive and "Aggress." or "Passive")
    end)
    botStatusFS = p:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    botStatusFS:SetPoint("TOPLEFT", 14, -136)
    botStatusFS:SetWidth(208)
    botStatusFS:SetJustifyH("LEFT")
    p:SetHeight(158)

    p:SetScript("OnUpdate", function(self, elapsed)
        self.acc = (self.acc or 0) + elapsed
        if self.acc < 0.1 then return end
        self.acc = 0
        local now = Now()
        if botDistDirtyAt and now - botDistDirtyAt >= 0.4 and botName then
            botDistDirtyAt = nil
            BotWhisper("distance " .. botDist)
        end
        if botSentAt and now - botSentAt > 4 then
            botSentAt = nil
            SetBotStatus("|cffff4040No answer: is " .. (botName or "it") .. " your bot?|r")
        end
    end)
    p:Hide()
end

local botFrame = CreateFrame("Frame")
botFrame:RegisterEvent("PLAYER_TARGET_CHANGED")
botFrame:RegisterEvent("CHAT_MSG_WHISPER")
for _, ev in ipairs({ "GROUP_ROSTER_UPDATE", "PARTY_MEMBERS_CHANGED", "RAID_ROSTER_UPDATE" }) do
    pcall(botFrame.RegisterEvent, botFrame, ev)
end
botFrame:SetScript("OnEvent", function(_, event, msg, author)
    if event == "CHAT_MSG_WHISPER" then
        -- In a battleground the client hands this handler secret strings for chat: nothing below may touch them (a match,
        -- compare or concatenation raised "attempt to perform string conversion on a secret string value" in WSG).
        if type(author) ~= "string" or type(msg) ~= "string" or IsSecret(author) or IsSecret(msg) then return end
        local who = strmatch(author, "^[^-]+")
        if not who or IsAssignMsg(msg) or not botName or who ~= botName then return end
        local confirm = ConfirmText(msg, author)
        knownBots[who] = true
        botSentAt = nil
        SetBotStatus("|cff00ff00" .. (confirm or msg) .. "|r")
        UpdateBotPanel()
        return
    end
    if event == "PLAYER_TARGET_CHANGED" then botForced = nil end
    if not botPanel then BuildBotPanel() end
    UpdateBotPanel()
end)

-- Unit right-click menu entry "ForeverBots": only through the client's own Menu.ModifyMenu API (a supported, taint-free
-- extension point on the 11.x-style UI). The old UnitPopupMenus / UnitPopup_ShowMenu tables are NOT touched: editing
-- them taints the protected popup buttons (Set Focus, Target...), so on a client without Menu.ModifyMenu this is skipped
-- and the floating panel (target-driven) is the way in.
-- Opens the client's chat box already in whisper mode for <name> (no slash text, so the client's slash parser is not involved:
-- on client 70291 in a battleground every slash line errors inside Blizzard's HandleChatType, "table cannot be indexed with
-- secret keys", Karrz 2026-10-09). Enter then sends an ordinary whisper packet. Opened a frame later because a typed "/w" runs
-- from the chat box, which the client closes after the handler returns.
local function OpenWhisperBox(name, text)
    local function Open()
        if ForeverBotsDB.bgWhisper == "dot" or not (ChatEdit_ChooseBoxForSend and ChatEdit_ActivateChat and ChatEdit_UpdateHeader) then
            if ChatFrame_OpenChat then ChatFrame_OpenChat(".w " .. name .. " " .. (text or "")) end
            return
        end
        local eb = ChatEdit_ChooseBoxForSend()
        eb:SetAttribute("chatType", "WHISPER")
        eb:SetAttribute("tellTarget", name)
        ChatEdit_UpdateHeader(eb)
        ChatEdit_ActivateChat(eb)
        eb:SetText(text or "")
    end
    if C_Timer and C_Timer.After then C_Timer.After(0.05, Open) else Open() end
end

-- Clicking a name in chat, a name's right-click Whisper, the friends / who / LFG lists all end in ChatFrameUtil.SendTellWithMessage,
-- which puts "<SLASH_WHISPER1> Name " in the chat box and asks the box to parse it. On 70291 SLASH_WHISPER1 is "/cw", which the
-- client does not turn into a whisper (Karrz 2026-10-09: clicking a name shows "Say: /cw Berolt"), so the box stays in Say mode with
-- the slash text in it. A post-hook (hooksecurefunc: the client's function is not replaced) then puts the box in whisper mode for
-- that name and clears the slash text. It runs synchronously, inside the click that started it: an addon may only touch the chat box
-- from a player action in a battleground (a timer callback there failed with "Interface action failed because of an AddOn" and left
-- "/cw Name" in the box, Karrz 2026-10-09). The client's own /w /whisper /t /tell are untouched.
local function HookSendTell()
    if not (ChatFrameUtil and ChatFrameUtil.SendTellWithMessage and hooksecurefunc) then return end
    hooksecurefunc(ChatFrameUtil, "SendTellWithMessage", function(name, text)
        name = SafeStr(name)
        if not name then return end
        local eb = ChatEdit_GetActiveWindow and ChatEdit_GetActiveWindow()
        if not eb then return end
        local ok, chatType = pcall(function() return eb:GetAttribute("chatType") end)
        if ok and (chatType == "WHISPER" or chatType == "BN_WHISPER") then return end   -- the client already did it
        local who = strtrim((strsplit("-", name)) or "")   -- "Name-Realm" -> "Name": a single realm
        if who == "" then return end
        Debug("whisper click: box was " .. tostring(chatType) .. ", switching to whisper mode for " .. who)
        eb:SetAttribute("chatType", "WHISPER")
        eb:SetAttribute("tellTarget", who)
        if ChatEdit_UpdateHeader then ChatEdit_UpdateHeader(eb) end
        eb:SetText(SafeStr(text) or "")
    end)
end

local function AddPopupEntry()
    if not (Menu and Menu.ModifyMenu) then return end
    local function modify(_, root, ctx)
        local name = ctx and SafeStr(ctx.name)
        if not name or name == SafeName("player") then return end
        local unit = ctx.unit
        if ctx.unit and UnitIsPlayer and not IsSecret(ctx.unit) then
            local who = strtrim((strsplit("-", name)) or "")
            if who ~= "" then
                root:CreateDivider()
                root:CreateButton("Whisper (Karrz's Player Bots)", function() OpenWhisperBox(who) end)
            end
        end
        if unit and not IsSecret(unit) then
            local ok, mine = pcall(InMyGroup, unit)
            if not ok or not mine then
                -- not in my group: the client's own Invite entry sends nothing for a bot (Karrz 2026-10-08, right-click Invite on a
                -- world bot; typed /invite works), so offer one that runs the typed command
                if ctx.unit and UnitIsPlayer and not UnitIsUnit(ctx.unit, "player") then
                    root:CreateDivider()
                    root:CreateButton("Invite (Karrz's Player Bots)", function()
                        local who = strsplit("-", name)
                        who = strtrim(who or "")
                        if who == "" then return end
                        if SlashCmdList and SlashCmdList.INVITE then
                            SlashCmdList.INVITE(who)
                        elseif C_PartyInfo and C_PartyInfo.InviteUnit then
                            C_PartyInfo.InviteUnit(who)
                        end
                    end)
                end
                return
            end
        end
        if IsSecret(unit) then unit = nil end
        root:CreateDivider()
        root:CreateButton("Karrz's Player Bots", function()
            local token = unit and select(2, UnitClass(unit)) or nil
            botForced = { name = name, token = token }
            if not botPanel then BuildBotPanel() end
            UpdateBotPanel()
        end)
    end
    for _, tag in ipairs({ "MENU_UNIT_PARTY", "MENU_UNIT_RAID_PLAYER", "MENU_UNIT_PLAYER", "MENU_UNIT_TARGET" }) do
        pcall(Menu.ModifyMenu, tag, modify)
    end
end

------------------------------------------------------------------------------------------------------------- slash

SLASH_FOREVERBOTS1 = "/fb"
SLASH_FOREVERBOTS2 = "/foreverbots"
SlashCmdList.FOREVERBOTS = function(msg)
    local verb, class, role, extra = strsplit(" ", strlower(strtrim(msg or "")))
    if verb == "chat" then
        if class == "on" then ForeverBotsDB.showChat = true
        elseif class == "off" then ForeverBotsDB.showChat = false
        else ForeverBotsDB.showChat = not ForeverBotsDB.showChat end
        Print("bot confirmations in chat: " .. (ForeverBotsDB.showChat and "shown" or "hidden (shown in the Bots panel status line)") .. "  (/fb chat on|off)")
        return
    end
    if verb == "debug" then
        if class == "on" then debugOn = true
        elseif class == "off" then debugOn = false
        else debugOn = not debugOn end
        Print("debug output: " .. (debugOn and "on (until you log out or /reload)" or "off") .. "  (/fb debug on|off)")
        return
    end
    if verb == "chatdiag" then
        -- Read-only dump of what the client thinks about chat, for finding why its own /w handler is missing (doc/whisper-feature-bisect.md)
        local function show(label, fn, ...)
            local ok, v = pcall(fn, ...)
            if not ok then v = "error: " .. tostring(v) elseif IsSecret(v) then v = "<secret>" end
            Print("diag " .. label .. " = " .. tostring(v))
        end
        local _, _, _, tocv = GetBuildInfo()
        Print("diag build " .. tostring(select(2, GetBuildInfo())) .. " toc " .. tostring(tocv))
        show("SlashCmdList.SMART_WHISPER", function() return type(SlashCmdList.SMART_WHISPER) end)
        show("SlashCmdList.WHISPER", function() return type(SlashCmdList.WHISPER) end)
        show("SlashCmdList.REPLY", function() return type(SlashCmdList.REPLY) end)
        show("SLASH_SMART_WHISPER1", function() return SLASH_SMART_WHISPER1 end)
        show("SLASH_WHISPER1", function() return SLASH_WHISPER1 end)
        show("hash_SlashCmdList[/w]", function() return type(hash_SlashCmdList and hash_SlashCmdList["/W"]) end)
        show("ChatTypeInfo.WHISPER", function() return type(ChatTypeInfo and ChatTypeInfo.WHISPER) end)
        show("RegionalUniqueNamesEnabled()", function() return RegionalUniqueNamesEnabled and tostring(RegionalUniqueNamesEnabled()) or "missing" end)
        show("autocomplete results for 'Name '", function()
            local n = UnitName("target")
            if not (n and C_AutoComplete and C_AutoComplete.GetAutoCompleteResults) then return "no target / api" end
            return tostring(#C_AutoComplete.GetAutoCompleteResults(n .. " ", 1, 0, true)) .. " for '" .. n .. " ', " ..
                tostring(#C_AutoComplete.GetAutoCompleteResults(n, 1, 0, true)) .. " for '" .. n .. "'"
        end)
        show("hash_ChatTypeInfoList[/cw]", function() return hash_ChatTypeInfoList and tostring(hash_ChatTypeInfoList["/CW"]) end)
        show("hash_ChatTypeInfoList[/r]", function() return hash_ChatTypeInfoList and tostring(hash_ChatTypeInfoList["/R"]) end)
        show("hash_ChatTypeInfoList[/w]", function() return hash_ChatTypeInfoList and tostring(hash_ChatTypeInfoList["/W"]) end)
        show("IsInInstance", function() local i, k = IsInInstance(); return tostring(i) .. " " .. tostring(k) end)
        if C_SocialRestrictions then
            for _, fn in ipairs({ "IsChatDisabled", "IsMuted", "IsSilenced", "IsSquelched", "IsRestrictedAccount", "IsRestricted" }) do
                if C_SocialRestrictions[fn] then show("C_SocialRestrictions." .. fn, C_SocialRestrictions[fn]) end
            end
        else
            Print("diag C_SocialRestrictions = nil")
        end
        if C_ChatInfo then
            for _, fn in ipairs({ "InChatMessagingLockdown", "CanPlayerSpeak", "IsChatLockdownActive", "IsAddonChatRestricted" }) do
                if C_ChatInfo[fn] then show("C_ChatInfo." .. fn, C_ChatInfo[fn]) end
            end
        end
        if C_Secrets then
            for _, fn in ipairs({ "HasSecretRestrictions", "ShouldCooldownsBeSecret" }) do
                if C_Secrets[fn] then show("C_Secrets." .. fn, C_Secrets[fn]) end
            end
        end
        show("ChatFrame1EditBox text secret", function() return IsSecret(ChatFrame1EditBox and ChatFrame1EditBox:GetText()) end)
        -- the client's unit-menu Whisper/Invite refuse units for which UnitIsHumanPlayer is false (unitpopupsharedbuttonmixins.lua:469/483)
        for _, fn in ipairs({ "UnitIsHumanPlayer", "UnitIsPlayer", "UnitIsConnected", "UnitIsFriend" }) do
            if _G[fn] then show(fn .. '("target")', _G[fn], "target") end
        end
        show("SendChatMessage", function() return type(SendChatMessage) end)
        show("C_ChatInfo.SendChatMessage", function() return type(C_ChatInfo and C_ChatInfo.SendChatMessage) end)
        return
    end
    if verb == "bgw" then
        ForeverBotsDB.bgWhisper = (class == "dot") and "dot" or "native"
        Print("battleground whisper mode: " .. ForeverBotsDB.bgWhisper .. "  (/fb bgw native|dot)")
        return
    end
    if verb == "assign" then
        -- /fb assign <name> <marker> [tank|cc|kill], /fb assign clear [marker|name], /fb assign list
        if class == "list" then
            printNextList = true
            RequestList()
        elseif class == "clear" then
            GroupOrder((role and role ~= "") and ("assign clear " .. role) or "assign clear")
            RequestListSoon()
        elseif class and class ~= "" and role and role ~= "" then
            GroupOrder("assign " .. class .. " " .. role .. ((extra and extra ~= "") and (" " .. extra) or ""))
            RequestListSoon()
        else
            Print("/fb assign <name|target> <star|circle|diamond|triangle|moon|square|cross|skull> [tank|cc|kill]; /fb assign clear [marker|name]; /fb assign list")
        end
        return
    end
    if verb == "bot" then
        -- /fb bot on|off: the floating per-bot panel that follows your target when it is one of your group's bots.
        if class == "on" or class == "off" then
            ForeverBotsDB.botPanel = (class == "on")
            if not botPanel then BuildBotPanel() end
            UpdateBotPanel()
        end
        Print("bot panel is " .. (BotPanelEnabled() and "on" or "off") .. ". /fb bot on|off")
        return
    end
    if verb == "tank" and class and class ~= "" then
        -- /fb tank <name> <marker>, /fb tank <name> clear, /fb tank clear
        if class == "clear" then GroupOrder("clear tanks")
        elseif role and role ~= "" then GroupOrder("tank " .. class .. " " .. role)
        else Print("/fb tank <name> <skull|cross|star|circle|diamond|triangle|moon|square|clear>; /fb tank clear") end
        return
    end
    if verb == "add" and class and class ~= "" then
        AddBot(class, role ~= "" and role or nil)
    elseif verb == "call" or verb == "dismiss" or verb == "save" or verb == "unsave" or verb == "regear" then
        ServerCommand(".bot " .. verb .. " " .. ((class and class ~= "") and class or "all"))
    elseif verb == "delete" and class and class ~= "" then
        ServerCommand(".bot delete " .. class)
    elseif verb == "list" then
        ServerCommand(".bot list")
    elseif verb == "fallback" then
        GroupOrder("fallback")
    elseif verb == "rally" then
        GroupOrder("rally here")
    elseif verb == "pullback" then
        GroupOrder("pull back")
    elseif verb == "formation" and class and class ~= "" then
        GroupOrder("formation " .. class)
    elseif verb == "spacing" and class and class ~= "" then
        GroupOrder("spacing " .. class)
    elseif verb == "distance" then
        local n = tonumber(class)
        if n then SetDistance(n)
        else Print("distance: " .. (ForeverBotsDB.distance and (ForeverBotsDB.distance .. " yards") or "auto") .. ". /fb distance <1-20> sets how far the bots follow you.") end
    elseif verb == "maintank" then
        GroupOrder((class and class ~= "") and ("set main tank " .. class) or "set main tank")
    elseif verb == "" or verb == nil then
        Toggle()
    else
        Print("/fb opens the window; /fb add <class> [tank|healer|dps]; /fb call|dismiss|save|unsave|regear [name|all]; /fb delete <name>; /fb list; /fb fallback; /fb pullback; /fb rally; /fb formation circle|arrow|line|none; /fb spacing tight|close|spread; /fb distance <1-20>; /fb maintank [name]; /fb bot on|off; /fb tank <name> <marker>|clear; /fb assign <name> <marker> [role]|clear|list; /fb debug on|off")
    end
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:SetScript("OnEvent", function()
    Print("type /fb for the bot window.")
    AddPopupEntry()
    HookSendTell()
    lastGroupSize = GroupSize()
    ShowDistance()
    if ForeverBotsDB.distance and lastGroupSize > 0 then distFrame.groupAt = Now() + 3 end
    if ForeverBotsDB.open then
        if not frame then BuildFrame() end
        frame:Show()
    end
end)

-- On client 70291 a battleground leaves the chat box broken: every typed slash line (also /fb, /w) then throws
-- ChatFrameEditBox.lua:206 "table cannot be indexed with secret keys" until /reload (Karrz 2026-10-09). Say so on leaving one.
do
    local pvpWas = false
    local pvpWatch = CreateFrame("Frame")
    pvpWatch:RegisterEvent("PLAYER_ENTERING_WORLD")
    pvpWatch:SetScript("OnEvent", function()
        local ok, inInstance, kind = pcall(IsInInstance)
        local now = ok and inInstance and (kind == "pvp" or kind == "arena") or false
        if pvpWas and not now then Debug("left the battleground: if typed / commands now show a Lua error, type /reload to clear the chat box.") end
        pvpWas = now
    end)
end

-- Diagnostic (70291 home party frame): print what the client itself believes about the group whenever it changes, so it can be read
-- in a battleground where typed slash commands fail. Remove once the party frame works on 70291.
do
    local lastLine
    local function Report(why)
        local function call(fn, ...)
            if type(fn) ~= "function" then return "n/a" end
            local ok, v = pcall(fn, ...)
            if not ok then return "err" end
            return tostring(v)
        end
        local line = string.format("group (%s): IsInGroup=%s IsInRaid=%s members=%s", why, call(IsInGroup), call(IsInRaid), call(GetNumGroupMembers))
        if line:gsub("^group %b():", "") ~= (lastLine or "") then
            lastLine = line:gsub("^group %b():", "")
            Debug(line)
        end
    end
    local groupWatch = CreateFrame("Frame")
    groupWatch:RegisterEvent("PLAYER_ENTERING_WORLD")
    pcall(groupWatch.RegisterEvent, groupWatch, "GROUP_ROSTER_UPDATE")
    groupWatch:SetScript("OnEvent", function(_, ev) Report(ev == "PLAYER_ENTERING_WORLD" and "entering world" or "roster update") end)
end
