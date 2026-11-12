// HumanLogin.h: what happens to the bot mark when a person logs into a character with the game client.
// Module::OnPlayerLogin drives every marked character. Summoning an alt into the party (add-on Log In, `.playerbot summon`)
// marks it in playerbot_v2_character, and only `.bot dismiss` took the mark off again; leaving or disbanding the group did not.
// So when the player later logged into that alt themselves, the bot logic attached to their own client: it moved, mounted and
// attacked on its own, gave control back whenever a movement key was pressed, then took over again (Discord report,
// 2026-10-27). `.playerbot self on` was saved the same way and came back at every login.
// A person at the keyboard is never driven unless they turn self-bot on in that session; the check runs at login and again at
// logout, so self-bot ends with the session. Pure; tested in
// tests/game/ClassicHumanLogin.cpp.
#pragma once

namespace Playerbot::V2
{

enum class HumanLoginAction
{
    None,       // not a bot, or a headless bot session: nothing to change
    Release,    // the player's own character (summoned alt, self-bot): unmark and unbind for good, as `.bot dismiss` does
    SkipThisLogin, // a bot pool character someone logged into by hand (GM): no bot logic now, it stays a bot in the database
};

// `marked`: the character is in the bot table. `headless`: the session is a bot session with no client.
// `on_pool_account`: the character's account is one of the bot pool accounts (BotAccountMgr::is_pool_account).
inline HumanLoginAction HumanClientAction(bool marked, bool headless, bool on_pool_account)
{
    if (!marked || headless)
        return HumanLoginAction::None;
    return on_pool_account ? HumanLoginAction::SkipThisLogin : HumanLoginAction::Release;
}

} // namespace Playerbot::V2
