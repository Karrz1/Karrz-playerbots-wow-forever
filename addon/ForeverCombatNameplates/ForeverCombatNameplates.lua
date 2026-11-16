local _, build = GetBuildInfo()
if (tonumber(build) or 0) < 70532 then return end

local prefix = "FCN_PLATES1"
local text = {
    noSwitch = "Forever Combat Nameplates: the client could not switch enemy nameplates.",
    off = "Forever Combat Nameplates: off. Turn Enemy Unit Nameplates on and Always Show Nameplates off (Esc > Options > Nameplates).",
    on = "Forever Combat Nameplates: on, the realm shows bars only for mobs fighting you or your pet.",
    waiting = "Forever Combat Nameplates: no answer from the realm yet, asking again. Bars show in combat until it answers.",
}
if GetLocale() == "ruRU" then
    text = {
        noSwitch = "Forever Combat Nameplates: клиент не смог переключить индикаторы противников.",
        off = "Forever Combat Nameplates: выключено. Включите индикаторы противников и выключите «Всегда показывать индикаторы» (Esc > Настройки > Индикаторы).",
        on = "Forever Combat Nameplates: включено, полоски здоровья только у противников, которые сражаются с вами или вашим питомцем.",
        waiting = "Forever Combat Nameplates: сервер ещё не ответил, запрос повторяется. До ответа полоски показываются в бою.",
    }
end
local enemies, always = "nameplateShowEnemies", "nameplateShowAll"
local events = CreateFrame("Frame")
local panel, desired, confirmed
local inWorld, inCombat, ownsOverride, refreshing = false, false, false, false
local attempts, generation, registered = 0, 0, false
local refresh

local function isSecret(value)
    return issecretvalue and issecretvalue(value)
end

local function register()
    if registered then return end
    local ok, result = pcall(C_ChatInfo.RegisterAddonMessagePrefix, prefix)
    registered = ok and result ~= false
end

local function sendMode(enabled)
    register()
    if not registered then return end
    local channel, recipient
    if IsInRaid and IsInRaid() then
        channel = "RAID"
    elseif IsInGroup and IsInGroup() then
        channel = "PARTY"
    else
        channel, recipient = "WHISPER", UnitName("player")
        if isSecret(recipient) or not recipient then return end
    end
    pcall(C_ChatInfo.SendAddonMessage, prefix, enabled and "ON" or "OFF", channel, recipient)
end

local function requestMode(enabled)
    desired = enabled
    generation = generation + 1
    local current = generation
    local function attempt()
        if current ~= generation or not inWorld then return end
        attempts = attempts + 1
        sendMode(enabled)
        if attempts < 3 and confirmed ~= desired then
            C_Timer.After(attempts * 2, attempt)
        end
    end
    confirmed, attempts = nil, 0
    attempt()
end

local warned = false
local function suppressEnemies(suppress)
    local ok = true
    if suppress and C_CVar.GetCVarBool(enemies) then
        ok = pcall(C_CVar.SetTempCVar, enemies, "0")
        if ok then
            ownsOverride = true
            ok = not C_CVar.GetCVarBool(enemies)
        end
    elseif not suppress and ownsOverride then
        ok = pcall(C_CVar.RemoveTempCVar, enemies)
        if ok then ownsOverride = false end
    end
    if not ok and not warned then
        warned = true
        print(text.noSwitch)
    end
end

refresh = function(retry)
    if refreshing then return end
    refreshing = true
    local settingsOpen = panel and panel:IsShown()
    if not inWorld or settingsOpen then suppressEnemies(false) end
    if inWorld then
        local enabled = not C_CVar.GetCVarBool(always)
            and (ownsOverride or C_CVar.GetCVarBool(enemies)) or false
        if desired ~= enabled or (retry and confirmed ~= desired and attempts >= 3) then
            requestMode(enabled)
        end
        -- Without an acknowledgement, retain the build 70532 client fallback.
        -- Once the realm filters each NPC, native plates stay enabled for pets too.
        suppressEnemies(enabled and confirmed ~= true and not inCombat and not settingsOpen)
    end
    refreshing = false
end

local function observeSettings()
    if panel or not SettingsPanel then return end
    panel = SettingsPanel
    panel:HookScript("OnShow", function() refresh() end)
    panel:HookScript("OnHide", function() refresh(true) end)
    events:UnregisterEvent("ADDON_LOADED")
end

local function ownReply(channel, sender)
    if channel ~= "WHISPER" or isSecret(sender) or not sender then return false end
    local me = UnitName("player")
    if isSecret(me) or not me then return false end
    if sender == me then return true end
    -- The realm part comes in different shapes ("Name-Realm", "Name-RealmNoSpaces", or "Name Realm" on some clients);
    -- character names never hold a space or a hyphen, so the part before the first one is the name.
    local name = sender:match("^([^%s%-]+)")
    return name == me
end

events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PLAYER_LEAVING_WORLD")
events:RegisterEvent("PLAYER_LOGOUT")
events:RegisterEvent("PLAYER_REGEN_DISABLED")
events:RegisterEvent("PLAYER_REGEN_ENABLED")
events:RegisterEvent("CVAR_UPDATE")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, name, message, channel, sender)
    if event == "CHAT_MSG_ADDON" then
        if not inWorld or isSecret(name) or name ~= prefix or isSecret(message)
            or not ownReply(channel, sender) then return end
        if message == "READY" then
            if desired ~= nil and confirmed ~= desired then requestMode(desired) end
        elseif (message == "ACK ON" and desired == true) or (message == "ACK OFF" and desired == false) then
            confirmed = desired
            generation = generation + 1
            refresh()
        end
        return
    elseif event == "ADDON_LOADED" then
        observeSettings()
        return
    elseif event == "PLAYER_ENTERING_WORLD" then
        observeSettings()
        inWorld = true
        inCombat = UnitAffectingCombat("player")
    elseif event == "PLAYER_LEAVING_WORLD" or event == "PLAYER_LOGOUT" then
        if inWorld and desired then sendMode(false) end
        inWorld, inCombat = false, false
        desired, confirmed = nil, nil
        generation = generation + 1
    elseif event == "PLAYER_REGEN_DISABLED" then
        inCombat = true
    elseif event == "PLAYER_REGEN_ENABLED" then
        inCombat = false
    elseif event == "CVAR_UPDATE" then
        name = name and name:lower()
        if name ~= "nameplateshowall" and name ~= "nameplateshowenemies" then return end
    end
    refresh(event == "PLAYER_REGEN_ENABLED")
end)

SLASH_FOREVERCOMBATNAMEPLATES1 = "/fcn"
SlashCmdList.FOREVERCOMBATNAMEPLATES = function()
    if not desired then
        print(text.off)
    elseif confirmed then
        print(text.on)
    else
        print(text.waiting)
        requestMode(true)
    end
end
