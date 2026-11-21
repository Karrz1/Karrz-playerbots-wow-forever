-- Migration: 0049_hall_of_thanes_route
-- Date:    2026-11-19
-- Purpose: The dungeon route (playerbot_dungeon_routes) of the Hall of Thanes (map 3065), built from the 3065 navmesh with its
--          tiles joined and with the doors as the official sniff shows them: shut doors block the way until the boss that opens
--          them is dead. The landing, round by the north (the straight hall's Iron Doors stay shut until the Ancient Switch) to
--          Faldrim Anvilmar, out of Anvilmar's Rest by the portcullises his death opens to Magmatus, back up to Plunder, through
--          the Iron Door his death opens and the Vault Door the tank opens to Durgen Dirgehammer. A crumb about every 15-20 y of
--          walked path (57 crumbs, about 1080 y, gaps up to 36 y; one 6 y before the Vault Door, where the tank opens it).
--          Every crumb-to-crumb leg is a clean path of at most 7 polygons that crosses no door still shut at that point. The order matches HallOfThanesScript.cpp. Without a route
--          the tank had only the boss spots to walk to, and the straight path to Faldrim runs into a shut door.
-- Reverts: yes (the map had no route before).

-- @target: shared

DELETE FROM {SHARED}.playerbot_dungeon_routes WHERE map_id = 3065;

INSERT INTO {SHARED}.playerbot_dungeon_routes (map_id, difficulty, seq, position_x, position_y, position_z) VALUES
    (3065,0,0,-95.62,-0.16,510.94),
    (3065,0,1,-76.00,2.60,505.29),
    (3065,0,2,-63.82,13.69,502.26),
    (3065,0,3,-43.60,3.93,499.69),
    (3065,0,4,-26.93,10.31,500.96),
    (3065,0,5,-11.09,9.12,500.22),
    (3065,0,6,-12.87,24.60,497.06),
    (3065,0,7,-12.00,53.33,491.82),
    (3065,0,8,-10.00,72.00,491.95),
    (3065,0,9,6.49,73.20,490.55),
    (3065,0,10,32.80,75.68,489.62),
    (3065,0,11,53.60,77.78,490.78),
    (3065,0,12,67.11,70.13,489.55),
    (3065,0,13,75.68,75.73,490.76),
    (3065,0,14,74.04,63.91,489.91),
    (3065,0,15,81.78,61.29,492.11),
    (3065,0,16,81.73,39.87,500.23),
    (3065,0,17,74.67,10.67,500.22),
    (3065,0,18,61.08,8.15,501.59),
    (3065,0,19,61.53,-7.53,501.57),
    (3065,0,20,72.21,-31.15,501.77),
    (3065,0,21,81.73,-51.47,494.48),
    (3065,0,22,71.53,-63.87,490.19),
    (3065,0,23,65.00,-79.13,490.47),
    (3065,0,24,54.09,-95.64,489.08),
    (3065,0,25,55.79,-115.73,487.42),
    (3065,0,26,68.62,-127.20,487.59),
    (3065,0,27,69.27,-149.93,487.42),
    (3065,0,28,75.56,-165.33,489.91),
    (3065,0,29,92.85,-164.91,489.34),
    (3065,0,30,107.64,-172.98,489.11),
    (3065,0,31,125.33,-176.80,487.68),
    (3065,0,32,137.17,-196.05,489.17),
    (3065,0,33,164.23,-195.54,486.29),
    (3065,0,34,160.00,-160.00,486.35),
    (3065,0,35,160.00,-138.67,485.95),
    (3065,0,36,156.93,-118.87,486.99),
    (3065,0,37,158.40,-100.98,488.84),
    (3065,0,38,157.73,-76.27,490.73),
    (3065,0,39,143.78,-70.40,492.33),
    (3065,0,40,141.29,-55.47,495.47),
    (3065,0,41,140.69,-34.45,501.99),
    (3065,0,42,135.20,-19.20,501.52),
    (3065,0,43,129.91,0.01,500.20),
    (3065,0,44,140.93,12.27,501.68),
    (3065,0,45,158.33,5.13,501.53),
    (3065,0,46,175.13,11.73,501.49),
    (3065,0,47,188.27,2.88,500.22),
    (3065,0,48,205.07,-1.60,500.34),
    (3065,0,49,224.00,-1.53,500.36),
    (3065,0,50,242.53,-2.36,501.57),
    (3065,0,51,262.87,-1.67,501.33),
    (3065,0,52,278.20,-9.13,502.37),
    (3065,0,53,301.60,-6.27,501.72),
    (3065,0,54,307.00,-0.80,500.50),
    (3065,0,55,319.29,-2.31,501.55),
    (3065,0,56,351.58,-9.77,500.79);
