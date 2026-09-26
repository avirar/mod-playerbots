-- ============================================================
-- HF Elevator Lift_01 (GO 186761, map 571) — walk-on nodes + transport leg fix
--
-- The lift had nodes (1987 bottom / 1988 top) but the top stop had NO walk-on
-- node (makeDockNode gap), so A* could not route through it and bots walked the
-- long way up the ramps. Adds the missing top walk-on, corrects the bottom
-- walk-on (stale 3871), and fixes the malformed 1987<->1988 transport path.
--
-- Walk-on points captured in-game (southern entrance, solid ground FloorZ):
--   bottom : (130.8943, -5766.3193, 38.4444)
--   top    : (130.8758, -5766.8027, 282.8496)
-- Platform stops (DBC keyframes): bottom (148.6,-5764.9,40.1) / top (148.6,-5764.9,284.6)
-- ============================================================

-- 1. Correct the bottom walk-on node (was 158.9,-5760.0 — wrong side).
UPDATE `playerbots_travelnode` SET x=130.8943, y=-5766.3193, z=38.4444 WHERE `id`=3871;

-- 2. Add the missing top walk-on node.
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES
(4419, 'Doodad_HF_Elevator_Lift_01 dock top', 571, 130.8758, -5766.8027, 282.8496, 1)
ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);

-- 3. Dock legs: walk-on <-> platform stop (type 3, object=0).
--    Bottom 3871<->1987 (re-point distance after the move); top 4419<->1988 (new).
UPDATE `playerbots_travelnode_link` SET distance=17.76
WHERE (node_id=3871 AND to_node_id=1987 AND type=3) OR (node_id=1987 AND to_node_id=3871 AND type=3);
INSERT IGNORE INTO `playerbots_travelnode_link`
(`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1988, 4419, 3, 0, 17.83, 0, 0, 1, 0, 0, 0),
(4419, 1988, 3, 0, 17.83, 0, 0, 1, 0, 0, 0);

-- 4. Walk leg from the top walk-on to the nearest catwalk node (Gate03 entry 3867).
INSERT IGNORE INTO `playerbots_travelnode_link`
(`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(4419, 3867, 1, 0, 10.78, 0, 0, 1, 60, 0, 0),
(3867, 4419, 1, 0, 10.78, 0, 0, 1, 60, 0, 0);

-- 5. Fix the malformed transport-leg path (1987<->1988): replace degenerate/empty
--    points with the two platform stops (bottom <-> top).
DELETE FROM `playerbots_travelnode_path` WHERE (node_id=1987 AND to_node_id=1988) OR (node_id=1988 AND to_node_id=1987);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES
(1987, 1988, 0, 571, 148.6, -5764.9, 40.1),
(1987, 1988, 1, 571, 148.6, -5764.9, 284.6),
(1988, 1987, 0, 571, 148.6, -5764.9, 284.6),
(1988, 1987, 1, 571, 148.6, -5764.9, 40.1);
