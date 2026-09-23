-- Remove an invalid one-way-portal link that made a Dalaran->Shattrath portal
-- appear bidirectional.
--
-- Link 3837 (Shattrath "Dalaran Portal to Shattrath", map 530) -> 4407
-- (Dalaran, map 571) via portal 191164 ("Portal to Dalaran") is invalid:
-- portal 191164 has no gameobject spawn, and the only real Dalaran<->Shattrath
-- portals (191013/191014) exist in Dalaran and lead TO Shattrath. There is no
-- Shattrath->Dalaran portal.
--
-- The invalid edge was cheap (0.1y portal hop, extra_cost 3), so A* routed
-- Shattrath -> 3837 -> 4407 (Dalaran) -> Darnassus, beating the direct
-- Shattrath Portal to Darnassus (4153 -> 3830, portal 183317) on cost. Bots
-- in Shattrath therefore never used the Darnassus portal.
--
-- The valid direction (4407 -> 3837, portal 191013) is kept. Idempotent: the
-- row simply will not match on DBs that never had it.
DELETE FROM `playerbots_travelnode_link`
WHERE `node_id` = 3837 AND `to_node_id` = 4407;
