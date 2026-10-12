# Headless packet-level test client for the 70291 realm (plan, nothing built yet)

Goal: log in, invite, trade, queue a battleground and inspect against a test realm without the WoW client, and fail a test when the server
sends something the real client's reader would not accept or when the server's behaviour is wrong.

## What it speaks (all of this exists in the server source, so the client can reuse it)

1. Realm login: the world socket checks a `RealmJoinTicket` against the database (`WorldSocket::HandleAuthSession`). The test seeds an account and a
   ticket directly in the test auth database; the Battle.net REST login is not under test.
2. World connection: `WORLD OF WARCRAFT CONNECTION - SERVER TO CLIENT - V2` / `CLIENT TO SERVER - V2` hello, `SMSG_AUTH_CHALLENGE`, `CMSG_AUTH_SESSION`
   (RegionID, BattlegroupID, RealmID, LocalChallenge[32], Digest[24], DosResponse, ticket), session key from the HMAC seeds in `WorldSocket.cpp`,
   `SMSG_ENTER_ENCRYPTED_MODE` (Ed25519 signed, region group from `Network.EnterEncryptedModeRegionGroup`), `CMSG_ENTER_ENCRYPTED_MODE_ACK`, then
   AES-GCM framed packets. The test realm sets `Network.SkipBuildAuthKeyCheck = 1`, as the live one does.
3. Opcode numbers: `ClassicOpcodes::TranslateServerOpcode` / `TranslateClientOpcode` for build 70291 (client side inverted).

## Decoding what the server sends

The client runs every received packet through the reader specs of the real client (`tests/game/data/Classic70291MainGroupSpecs.inc`, the same table the
golden byte tests use). A packet the reader does not consume exactly is a test failure with the opcode name, the byte offset and a hex dump. Limits:
the flat spec shows a loop body once, so packets with containers are checked only when the count is 1 until the specs get loop structure; unresolved
(`?`) and undecrypted (`!`) messages are counted and listed, not failed.

## Scenarios (each a Catch2 test, one login each; a test realm with a few bots)

| Scenario | Steps | Asserts |
|---|---|---|
| login | auth, char enum, `CMSG_PLAYER_LOGIN` | every login packet consumed, the feature status flag run as the client reads it |
| invite | `CMSG_PARTY_INVITE_BY_NAME` a bot, accept | `PARTY_UPDATE` with 2 members, full state for the bot, leader flags |
| trade | initiate, add item, accept | `TRADE_STATUS` sequence and `TRADE_UPDATED` consumed |
| battleground | `CMSG_BATTLEMASTER_JOIN`, wait for invite, enter | status packets, positions, flag carrier updates consumed |
| inspect | `CMSG_INSPECT` of a bot | `INSPECT_RESULT` consumed (needs the inspect reader decoded first) |
| whisper | `CMSG_CHAT_MESSAGE_WHISPER` to a bot | bot reply arrives as `SMSG_CHAT` |

## Where it runs

On the PC, in the existing build tree, as a small extra target (`headless_client`, no new libraries: asio, OpenSSL, Catch2 are already linked), against
a second worldserver started from the same binary with its own throwaway databases and config (different ports, no world bots except the scenario's).
It adds one compile of a small target, no new tools, and nothing outside the repo and the temp databases; the databases are dropped after the run.
Cloud cannot run it, because the cloud container has no server binary; the cloud writes the code and the PC build-verifies, as for the other tests.

## Order of work

1. Framing + auth + encrypted mode against the test realm (login scenario only). This is the risky part and the first thing to prove.
2. Spec-checked receive loop and the invite and trade scenarios (their specs are resolved today).
3. Battleground, inspect, whisper once their readers are decoded from the longer dump.
4. A one-command runner (`scripts\run-headless.ps1`) that builds nothing, starts the test realm, runs the scenarios, stops the realm and drops the databases.

## Open points (need an answer before step 1)

- The 70291 client-to-server packets (`CMSG_AUTH_SESSION` body layout for 70291, `CMSG_PLAYER_LOGIN` etc.) are the same writers the server reads; the
  client must produce them in the 70291 layout. Where a server reader is retail-shaped, the test client writes what our reader accepts, which proves the
  server side only. The real client's writers are in the dump (the client send path), decoded the same way once the longer dump exists.
- The test realm needs its own `realmlist` row and an account with a ticket; the seed script goes in `sql/custom/`.
