-- Auth database of the headless test realm ONLY (a throwaway database, never the live one): one game account the headless client logs in
-- with. The 64 byte key is what the Battle.net login would store in account.session_key_bnet; the client derives the world session key
-- from it, so both sides must hold the same bytes (HEADLESS_KEY = the hex below).
--
-- SAFETY: the key is public and the account is GM level 3. Every statement below inserts NOTHING unless the current database name
-- contains "headless" or "test" (case-insensitive), so running this file by hand against a real auth database is a no-op.
-- Name the throwaway auth database accordingly (for example auth_headless_test).
INSERT INTO `battlenet_accounts` (`id`, `email`, `srp_version`, `salt`, `verifier`)
    SELECT 900001, 'headless@test.local', 1, UNHEX(REPEAT('00', 32)), ''
    FROM DUAL WHERE LOWER(DATABASE()) REGEXP 'headless|test';

INSERT INTO `account` (`id`, `username`, `salt`, `verifier`, `session_key_bnet`, `email`, `reg_mail`, `expansion`, `client_build`, `battlenet_account`, `battlenet_index`)
    SELECT 900001, '900001#1', UNHEX(REPEAT('00', 32)), UNHEX(REPEAT('00', 32)), UNHEX(REPEAT('A5', 64)), 'headless@test.local', '', 11, 70291, 900001, 1
    FROM DUAL WHERE LOWER(DATABASE()) REGEXP 'headless|test';

INSERT INTO `account_access` (`AccountID`, `SecurityLevel`, `RealmID`)
    SELECT 900001, 3, -1
    FROM DUAL WHERE LOWER(DATABASE()) REGEXP 'headless|test';
