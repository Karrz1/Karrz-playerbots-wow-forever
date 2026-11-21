--[[
ForeverBots_MicroMenu: keeps the Housing Dashboard and Adventure Guide buttons off the micro menu (public issue #18).

WoW Forever has neither feature. The client's micro menu (Blizzard_MicroMenu, Camelot override of GenerateButtonInfos) leaves
a button out when the server's game rule for it is on: HousingDashboardDisabled (102) and EncounterJournalDisabled (42). Our
server sends both, as the official realm does, yet some players still see the two buttons, and without a client to look at we
do not know why yet. So this file is a safety net: when either button is shown it is hidden again and the menu is laid out
anew, which closes the gap. "/fb rules" prints what the client thinks of the two rules, for the next bug report.
]]

local ADDON, ns = ...

local NAMES = { "HousingMicroButton", "EJMicroButton" }

local relayoutAfterCombat = false
local pending = false
local timesHidden = 0   -- how often a button had to be hidden this session, for /fb rules

local function Relayout()
    if not (MicroMenu and MicroMenu.Layout) then return end
    if InCombatLockdown() then
        relayoutAfterCombat = true
        return
    end
    pcall(MicroMenu.Layout, MicroMenu)
end

-- Hide whichever of the two buttons is shown; lay the menu out again only when one was.
local function Apply()
    pending = false
    local hidden = false
    for _, name in ipairs(NAMES) do
        local button = _G[name]
        if button and button:IsShown() then
            button:Hide()
            hidden = true
            timesHidden = timesHidden + 1
        end
    end
    if hidden then Relayout() end
end

-- From an OnShow hook, wait for the frame that showed the button to finish (its own layout runs in the same call).
local function ApplySoon()
    if pending then return end
    pending = true
    C_Timer.After(0, Apply)
end

local hooked = {}
local function HookButtons()
    for _, name in ipairs(NAMES) do
        local button = _G[name]
        if button and not hooked[name] then
            button:HookScript("OnShow", ApplySoon)   -- HookScript keeps the client's own OnShow (the housing button has one)
            hooked[name] = true
        end
    end
end

local events = CreateFrame("Frame")
-- Loading screens and the vehicle / override bars move the micro menu and can show the buttons again.
for _, event in ipairs({ "PLAYER_LOGIN", "PLAYER_ENTERING_WORLD", "UPDATE_OVERRIDE_ACTIONBAR", "UPDATE_VEHICLE_ACTIONBAR",
        "UNIT_EXITED_VEHICLE", "PLAYER_REGEN_ENABLED" }) do
    events:RegisterEvent(event)
end
events:SetScript("OnEvent", function(_, event, unit)
    if event == "UNIT_EXITED_VEHICLE" and unit ~= "player" then return end
    if event == "PLAYER_REGEN_ENABLED" then
        if relayoutAfterCombat then
            relayoutAfterCombat = false
            Relayout()
        end
        return
    end
    HookButtons()
    ApplySoon()
end)

ns.MicroMenu = {}

-- "/fb rules": the two game rules as the client sees them and the enum numbers it uses for them (want: true true 102 42).
function ns.MicroMenu.Report(print)
    local function rule(key)
        local id = Enum and Enum.GameRule and Enum.GameRule[key]
        local ok, active = pcall(function() return C_GameRules.IsGameRuleActive(id) end)
        return tostring(id), ok and tostring(active) or ("error: " .. tostring(active))
    end
    local housingId, housingActive = rule("HousingDashboardDisabled")
    local ejId, ejActive = rule("EncounterJournalDisabled")
    print(("rules: HousingDashboardDisabled %s = %s, EncounterJournalDisabled %s = %s"):format(housingId, housingActive, ejId, ejActive))
    for _, name in ipairs(NAMES) do
        local button = _G[name]
        print(("rules: %s %s"):format(name, button and (button:IsShown() and "shown" or "hidden") or "missing"))
    end
    print(("rules: buttons hidden by ForeverBots this session: %d"):format(timesHidden))
end
