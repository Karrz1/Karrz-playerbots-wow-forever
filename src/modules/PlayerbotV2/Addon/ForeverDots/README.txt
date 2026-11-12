ForeverDots 1.2.1
=================

Countdown bars for your own damage-over-time spells (Corruption, Immolate,
Siphon Life, Curse of Agony and so on, any class), beside your character,
grouped per enemy: the enemy's name (your current target in gold), then one
bar per spell with its icon and the seconds left. Bars turn red in the last
3 seconds.

How it works: this client does not let add-ons read enemy debuffs in combat,
so each bar starts when your spell finishes casting on your target, using
the duration in the spell's tooltip. Casting it again on the same enemy
restarts the bar. Things it cannot know: a resist, a dispel, or an enemy
dying when it is not your target (its bars just run out).

Install
-------
The WoW Forever Bots repack installs it together with ForeverBots (install,
update and every Play from the launcher replace the folder with a clean copy).
By hand:
1. Close the game (or be ready to /reload).
2. Unzip so this folder ends up as:
   <game folder>\_forever_\Interface\AddOns\ForeverDots\ForeverDots.toc
   (replace the old ForeverDots folder)
3. Start the game, make sure ForeverDots is ticked in the AddOns list.

Commands
--------
/dots test               sample bars, to place it
/dots unlock             show the purple box, drag it where you want it
/dots lock               lock it again
/dots reset              back to the default spot (right of your character)
/dots red 3              seconds left when bars turn red
/dots scale 1.2          size (0.5 to 2)
/dots width 240          bar width (100 to 400)
/dots clear              remove all bars
/dots spells             the spells it has found and their durations
/dots time Corruption 18 set a spell's duration by hand (0 = use tooltip)
