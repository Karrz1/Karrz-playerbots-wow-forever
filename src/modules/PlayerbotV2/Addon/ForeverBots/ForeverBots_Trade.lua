--[[
ForeverBots_Trade: one click on Trade accepts a trade with a bot.

The stock Trade button sends the offer state the client last heard of. A bot answers an offer in the same moment (puts its side
in, changes it, accepts), so the click often carries an older state and the server answers "the trade has changed", click after
click. Right after the stock click this file sends what the window shows (both sides' money, the item and count in each of the
six traded slots) as `.bot trade <seq> <what you see>`. When the window still holds exactly that, the server accepts for you
with the bot's current state; when it does not, nothing is accepted and you are told to look again. A trade with a player is
left to the stock click alone (the server answers "none").

The command goes the way every click command of the add-on goes (ns.Say): alone as a whisper to yourself, which the server runs
as a dot command before any whisper is made; in a group as an add-on message on the group channel. The answer is a hidden
system line (TRADE_RESP, AddonControl.cpp), which reaches a solo client. Nothing here runs from a timer or an event.
]]

local _, ns = ...
local net = ns.Net
if not net then return end
net.ANSWERS.TRADE_RESP = true
local L = ns.L or setmetatable({}, { __index = function(_, k) return (k:gsub("^short:", "")) end })

local SLOTS = 6   -- traded slots; slot 7 ("will not be traded") does not change hands

local function ItemId(link)
    return type(link) == "string" and tonumber(link:match("item:(%d+)")) or 0
end

-- One side as the server writes it (TradeSideText): money, then entry:count per traded slot, 0:0 for an empty one.
local function Side(linkOf, infoOf, money)
    local out = { tostring(math.floor(tonumber(money) or 0)) }
    for i = 1, SLOTS do
        local id = ItemId(linkOf and linkOf(i))
        local count = 0
        if id > 0 then
            local _, _, n = infoOf(i)
            count = tonumber(n) or 1
        end
        out[#out + 1] = tostring(id)
        out[#out + 1] = tostring(count)
    end
    return table.concat(out, ":")
end

local function Seen()
    if not (GetTradeTargetItemLink and GetTradePlayerItemLink and GetTargetTradeMoney and GetPlayerTradeMoney) then return nil end
    return Side(GetTradeTargetItemLink, GetTradeTargetItemInfo, GetTargetTradeMoney()) .. ":" ..
        Side(GetTradePlayerItemLink, GetTradePlayerItemInfo, GetPlayerTradeMoney())
end

local function Answered(fields)
    if fields and fields[1] == "changed" and UIErrorsFrame then
        UIErrorsFrame:AddMessage(L["The bot changed its offer: look at it again, then click Trade."], 1, 0.6, 0)
    end
end

-- after the stock click, from the click itself
local function AfterTradeClick()
    if not (ns.Say and TradeFrame and TradeFrame:IsShown()) then return end
    local seen = Seen()
    if not seen then return end
    net.seq = (net.seq % 0x7ffffffe) + 1
    local seq = net.seq
    net.pending[seq] = { cb = Answered, mtype = "TRADE_RESP", at = GetTime() }
    ns.Say(string.format(".bot trade %x %s", seq, seen))
end

local hooked = false
local function Hook()
    local button = TradeFrameTradeButton or (TradeFrame and TradeFrame.TradeButton)
    if hooked or not button or not button.HookScript then return end
    button:HookScript("OnClick", AfterTradeClick)
    hooked = true
end

local f = CreateFrame("Frame")
f:RegisterEvent("TRADE_SHOW")
f:RegisterEvent("PLAYER_LOGIN")
f:SetScript("OnEvent", Hook)
