local _, ns = ...
local net = ns.Net
net.ANSWERS.BOT_TRADE_RESP = true

local events = CreateFrame("Frame")
local version, open, button, token, accepted = 0, false, nil, nil, false
local function stockButton()
    return TradeFrameTradeButton or (TradeFrame and TradeFrame.TradeButton)
end

-- Solo clients on this branch can drop addon whispers. A normal whisper to
-- oneself carries only the request. The server answers privately via PBC.
local function request(kind, fields, cb)
    if IsInGroup() then return net.Request(kind, fields, cb) end
    net.seq = (net.seq % 0x7ffffffe) + 1
    local seq = net.seq
    net.pending[seq] = { cb = cb, mtype = kind, at = GetTime() }
    local send = SendChatMessage or (C_ChatInfo and C_ChatInfo.SendChatMessage)
    if not send or not pcall(send, string.format("FBTrade %x %s", seq, fields[1] or "view"),
            "WHISPER", nil, UnitName("player")) then
        net.pending[seq] = nil
        cb(nil)
    end
end

local function hideProtocol(_, _, message, sender)
    return sender and Ambiguate(sender, "short") == UnitName("player") and
        message:match("^FBTrade %x+ [%w:]+$") ~= nil
end
ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER", hideProtocol)
ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER_INFORM", hideProtocol)

local refresh
local function click()
    if not open or not token then return end
    if accepted then
        CancelTradeAccept()
        refresh()
        return
    end
    local clicked, v = token, version
    token = nil
    button:Disable()
    request("BOT_TRADE_ACCEPT", {clicked}, function(fields, kind)
        if not open or v ~= version then return end
        if not fields or kind ~= "BOT_TRADE_RESP" or fields[1] ~= "submitted" then
            UIErrorsFrame:AddMessage(ERR_TRADE_REQUEST_CHANGED or "Please review the trade again.", 1, 0.6, 0)
        end
        refresh()
    end)
end

local function ensureButton(stock)
    if button then return end
    button = CreateFrame("Button", nil, stock:GetParent(), "UIPanelButtonTemplate")
    button:SetAllPoints(stock)
    button:SetFrameLevel(stock:GetFrameLevel() + 5)
    button:SetScript("OnClick", click)
end

refresh = function()
    version = version + 1
    local v = version
    token = nil
    if button and button:IsShown() then button:Disable() end
    if not open then
        if button then button:Hide() end
        return
    end
    local stock = stockButton()
    if stock and stock:IsVisible() then
        ensureButton(stock)
        button:SetText(stock:GetText())
        button:Disable()
        button:Show()
    end
    C_Timer.After(0.12, function()
        if v ~= version or not open then return end
        request("BOT_TRADE_VIEW", {}, function(fields, kind)
            if v ~= version or not open then return end
            local stock = stockButton()
            local partner = UnitName("NPC") or (TradeFrameRecipientNameText and TradeFrameRecipientNameText:GetText())
            if kind ~= "BOT_TRADE_RESP" or not fields or fields[1] ~= "ready" or
                partner ~= fields[3] or not stock or not stock:IsVisible() then
                if button then button:Hide() end
                return
            end
            ensureButton(stock)
            token, accepted = fields[2], fields[4] == "1"
            button:SetText(accepted and (CANCEL or "Cancel") or stock:GetText())
            button:SetEnabled(accepted or stock:IsEnabled())
            button:Show()
        end)
    end)
end

for _, event in ipairs({"TRADE_SHOW", "TRADE_CLOSED", "TRADE_PLAYER_ITEM_CHANGED",
    "TRADE_TARGET_ITEM_CHANGED", "TRADE_MONEY_CHANGED", "TRADE_ACCEPT_UPDATE"}) do
    events:RegisterEvent(event)
end
events:SetScript("OnEvent", function(_, event)
    if event == "TRADE_SHOW" then open = true end
    if event == "TRADE_CLOSED" then open = false end
    refresh()
end)
