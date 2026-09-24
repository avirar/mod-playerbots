-- ============================================================
-- Ship/Zeppelin/Turtle dock (wait) nodes for every terminus lacking one.
-- Wait points probed server-side (vmap/WMO-aware, travel.boatgen) or
-- captured in-game by the owner (Feathermoon Ferry, Mighty Wind m571).
-- ============================================================

-- ---- Ship, Icebreaker (Stormwinds Pride)dock (m0)  deck 2032 <-> dock 4409 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4409, 'Ship, Icebreaker (Stormwinds Pride)dock', 0, -8295.6143, 1406.9562, 4.3721, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2032, 4409, 3, 0, 19.00, 0, 0, 1, 0, 0, 0),
(4409, 2032, 3, 0, 19.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2465, 4409, 1, 0, 221.44, 0, 0, 1, 60, 0, 0),
(4409, 2465, 1, 0, 221.44, 0, 0, 1, 60, 0, 0);
-- walk path 2465 -> 4409 (fwd 57 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 0, 0, -8364.2002, 1227.8300, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 1, 0, -8360.4141, 1226.5409, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 2, 0, -8356.6270, 1225.2526, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 3, 0, -8352.6309, 1225.0823, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 4, 0, -8348.6348, 1224.9122, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 5, 0, -8346.2598, 1228.1305, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 6, 0, -8343.8838, 1231.3488, 5.2896);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 7, 0, -8341.5088, 1234.5671, 4.5474);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 8, 0, -8339.1328, 1237.7853, 4.5474);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 9, 0, -8335.6406, 1239.7360, 4.5474);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 10, 0, -8332.1484, 1241.6866, 5.4189);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 11, 0, -8328.6562, 1243.6373, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 12, 0, -8325.1641, 1245.5880, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 13, 0, -8321.6719, 1247.5387, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 14, 0, -8318.1797, 1249.4894, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 15, 0, -8314.6875, 1251.4401, 5.8940);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 16, 0, -8311.1953, 1253.3909, 5.9264);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 17, 0, -8307.7031, 1255.3418, 4.3720);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 18, 0, -8306.8340, 1259.2462, 6.0371);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 19, 0, -8305.9639, 1263.1505, 6.1108);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 20, 0, -8305.3516, 1267.1034, 6.1626);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 21, 0, -8304.7393, 1271.0563, 6.2144);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 22, 0, -8304.1270, 1275.0092, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 23, 0, -8303.5146, 1278.9620, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 24, 0, -8302.9023, 1282.9149, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 25, 0, -8302.2900, 1286.8678, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 26, 0, -8301.6777, 1290.8207, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 27, 0, -8301.0654, 1294.7736, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 28, 0, -8300.4541, 1298.7264, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 29, 0, -8299.8418, 1302.6793, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 30, 0, -8299.2305, 1306.6323, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 31, 0, -8298.6182, 1310.5852, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 32, 0, -8298.0068, 1314.5382, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 33, 0, -8297.9033, 1318.5369, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 34, 0, -8297.7998, 1322.5355, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 35, 0, -8297.6963, 1326.5342, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 36, 0, -8297.5928, 1330.5328, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 37, 0, -8297.4893, 1334.5315, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 38, 0, -8297.3857, 1338.5302, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 39, 0, -8297.2822, 1342.5288, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 40, 0, -8297.1787, 1346.5275, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 41, 0, -8297.0752, 1350.5261, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 42, 0, -8296.9717, 1354.5248, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 43, 0, -8296.8682, 1358.5234, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 44, 0, -8296.7646, 1362.5221, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 45, 0, -8296.6611, 1366.5208, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 46, 0, -8296.5576, 1370.5194, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 47, 0, -8296.4541, 1374.5181, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 48, 0, -8296.3506, 1378.5167, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 49, 0, -8296.2471, 1382.5154, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 50, 0, -8296.1436, 1386.5140, 5.1209);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 51, 0, -8296.0400, 1390.5127, 4.8241);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 52, 0, -8295.9365, 1394.5114, 4.5274);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 53, 0, -8295.8330, 1398.5100, 4.4458);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 54, 0, -8295.7295, 1402.5087, 4.4109);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 55, 0, -8295.6260, 1406.5073, 4.3760);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2465, 4409, 56, 0, -8295.6143, 1406.9562, 4.3721);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 0, 0, -8295.6143, 1406.9562, 4.3721);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 1, 0, -8295.6260, 1406.5073, 4.3760);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 2, 0, -8295.7295, 1402.5087, 4.4109);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 3, 0, -8295.8330, 1398.5100, 4.4458);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 4, 0, -8295.9365, 1394.5114, 4.5274);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 5, 0, -8296.0400, 1390.5127, 4.8241);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 6, 0, -8296.1436, 1386.5140, 5.1209);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 7, 0, -8296.2471, 1382.5154, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 8, 0, -8296.3506, 1378.5167, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 9, 0, -8296.4541, 1374.5181, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 10, 0, -8296.5576, 1370.5194, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 11, 0, -8296.6611, 1366.5208, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 12, 0, -8296.7646, 1362.5221, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 13, 0, -8296.8682, 1358.5234, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 14, 0, -8296.9717, 1354.5248, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 15, 0, -8297.0752, 1350.5261, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 16, 0, -8297.1787, 1346.5275, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 17, 0, -8297.2822, 1342.5288, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 18, 0, -8297.3857, 1338.5302, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 19, 0, -8297.4893, 1334.5315, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 20, 0, -8297.5928, 1330.5328, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 21, 0, -8297.6963, 1326.5342, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 22, 0, -8297.7998, 1322.5355, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 23, 0, -8297.9033, 1318.5369, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 24, 0, -8298.0068, 1314.5382, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 25, 0, -8298.6182, 1310.5852, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 26, 0, -8299.2305, 1306.6323, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 27, 0, -8299.8418, 1302.6793, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 28, 0, -8300.4541, 1298.7264, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 29, 0, -8301.0654, 1294.7736, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 30, 0, -8301.6777, 1290.8207, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 31, 0, -8302.2900, 1286.8678, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 32, 0, -8302.9023, 1282.9149, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 33, 0, -8303.5146, 1278.9620, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 34, 0, -8304.1270, 1275.0092, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 35, 0, -8304.7393, 1271.0563, 6.2144);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 36, 0, -8305.3516, 1267.1034, 6.1626);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 37, 0, -8305.9639, 1263.1505, 6.1108);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 38, 0, -8306.8340, 1259.2462, 6.0371);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 39, 0, -8307.7031, 1255.3418, 4.3720);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 40, 0, -8311.1953, 1253.3909, 5.9264);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 41, 0, -8314.6875, 1251.4401, 5.8940);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 42, 0, -8318.1797, 1249.4894, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 43, 0, -8321.6719, 1247.5387, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 44, 0, -8325.1641, 1245.5880, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 45, 0, -8328.6562, 1243.6373, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 46, 0, -8332.1484, 1241.6866, 5.4189);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 47, 0, -8335.6406, 1239.7360, 4.5474);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 48, 0, -8339.1328, 1237.7853, 4.5474);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 49, 0, -8341.5088, 1234.5671, 4.5474);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 50, 0, -8343.8838, 1231.3488, 5.2896);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 51, 0, -8346.2598, 1228.1305, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 52, 0, -8348.6348, 1224.9122, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 53, 0, -8352.6309, 1225.0823, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 54, 0, -8356.6270, 1225.2526, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 55, 0, -8360.4141, 1226.5409, 5.2302);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4409, 2465, 56, 0, -8364.2002, 1227.8300, 5.2302);

-- ---- Ship, Icebreaker (Stormwinds Pride)dock (m571)  deck 2033 <-> dock 4410 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4410, 'Ship, Icebreaker (Stormwinds Pride)dock', 571, 2232.1184, 5132.7246, 5.3442, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2033, 4410, 3, 0, 19.00, 0, 0, 1, 0, 0, 0),
(4410, 2033, 3, 0, 19.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(32, 4410, 1, 0, 77.02, 0, 0, 1, 60, 0, 0),
(4410, 32, 1, 0, 77.02, 0, 0, 1, 60, 0, 0);
-- walk path 32 -> 4410 (fwd 20 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 0, 571, 2272.9800, 5171.8198, 11.2460);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 1, 571, 2269.6453, 5174.3423, 11.1171);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 2, 571, 2266.3435, 5176.6001, 11.2219);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 3, 571, 2263.0420, 5178.8584, 11.1959);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 4, 571, 2259.2861, 5180.2344, 11.3848);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 5, 571, 2255.3474, 5180.9312, 11.5679);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 6, 571, 2251.8928, 5178.9150, 11.7701);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 7, 571, 2248.4390, 5176.8975, 11.7701);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 8, 571, 2247.0527, 5173.1455, 11.7707);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 9, 571, 2245.6665, 5169.3936, 11.7720);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 10, 571, 2244.2803, 5165.6416, 11.7749);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 11, 571, 2242.8940, 5161.8896, 9.7530);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 12, 571, 2241.5078, 5158.1377, 6.6810);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 13, 571, 2240.1216, 5154.3857, 5.3738);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 14, 571, 2238.7354, 5150.6338, 5.3560);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 15, 571, 2237.3491, 5146.8818, 5.3454);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 16, 571, 2235.9629, 5143.1299, 5.3454);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 17, 571, 2234.5767, 5139.3779, 5.3451);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 18, 571, 2233.1904, 5135.6260, 5.3446);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (32, 4410, 19, 571, 2232.1184, 5132.7246, 5.3442);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 0, 571, 2232.1184, 5132.7246, 5.3442);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 1, 571, 2233.1904, 5135.6260, 5.3446);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 2, 571, 2234.5767, 5139.3779, 5.3451);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 3, 571, 2235.9629, 5143.1299, 5.3454);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 4, 571, 2237.3491, 5146.8818, 5.3454);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 5, 571, 2238.7354, 5150.6338, 5.3560);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 6, 571, 2240.1216, 5154.3857, 5.3738);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 7, 571, 2241.5078, 5158.1377, 6.6810);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 8, 571, 2242.8940, 5161.8896, 9.7530);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 9, 571, 2244.2803, 5165.6416, 11.7749);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 10, 571, 2245.6665, 5169.3936, 11.7720);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 11, 571, 2247.0527, 5173.1455, 11.7707);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 12, 571, 2248.4390, 5176.8975, 11.7701);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 13, 571, 2251.8928, 5178.9150, 11.7701);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 14, 571, 2255.3474, 5180.9312, 11.5679);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 15, 571, 2259.2861, 5180.2344, 11.3848);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 16, 571, 2263.0420, 5178.8584, 11.1959);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 17, 571, 2266.3435, 5176.6001, 11.2219);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 18, 571, 2269.6453, 5174.3423, 11.1171);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4410, 32, 19, 571, 2272.9800, 5171.8198, 11.2460);

-- ---- Ship, Icebreaker (Northspear)dock (m0)  deck 1969 <-> dock 4411 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4411, 'Ship, Icebreaker (Northspear)dock', 0, -3718.2563, -602.0776, 5.1807, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1969, 4411, 3, 0, 33.00, 0, 0, 1, 0, 0, 0),
(4411, 1969, 3, 0, 33.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2755, 4411, 1, 0, 177.08, 0, 0, 1, 60, 0, 0),
(4411, 2755, 1, 0, 177.08, 0, 0, 1, 60, 0, 0);
-- walk path 2755 -> 4411 (fwd 46 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 0, 0, -3744.9900, -759.7520, 9.6740);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 1, 0, -3744.6597, -755.7657, 9.1571);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 2, 0, -3744.3293, -751.7794, 8.7953);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 3, 0, -3743.9990, -747.7930, 8.4891);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 4, 0, -3743.6687, -743.8067, 8.2464);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 5, 0, -3743.3381, -739.8204, 8.2925);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 6, 0, -3743.0078, -735.8340, 7.5060);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 7, 0, -3742.6772, -731.8477, 8.2609);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 8, 0, -3742.6279, -727.8480, 8.3214);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 9, 0, -3742.5786, -723.8483, 8.3311);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 10, 0, -3742.5293, -719.8486, 8.3061);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 11, 0, -3742.4800, -715.8489, 8.2811);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 12, 0, -3742.4309, -711.8492, 8.2560);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 13, 0, -3742.3816, -707.8495, 8.2747);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 14, 0, -3743.3435, -703.9669, 8.4178);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 15, 0, -3744.3054, -700.0843, 8.5600);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 16, 0, -3745.2673, -696.2017, 8.7022);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 17, 0, -3746.2292, -692.3190, 8.8444);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 18, 0, -3747.1912, -688.4364, 8.9204);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 19, 0, -3748.1531, -684.5538, 8.8778);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 20, 0, -3749.1150, -680.6711, 8.8351);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 21, 0, -3750.0769, -676.7885, 8.7828);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 22, 0, -3751.0388, -672.9059, 8.7252);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 23, 0, -3752.0007, -669.0233, 8.6676);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 24, 0, -3752.9624, -665.1406, 8.6142);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 25, 0, -3753.9243, -661.2580, 8.5741);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 26, 0, -3754.8860, -657.3754, 8.5173);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 27, 0, -3755.8479, -653.4927, 8.4511);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 28, 0, -3756.8096, -649.6101, 8.3849);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 29, 0, -3757.7715, -645.7275, 8.3187);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 30, 0, -3758.7332, -641.8448, 8.2707);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 31, 0, -3755.8799, -639.0415, 7.9957);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 32, 0, -3753.0266, -636.2382, 7.7207);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 33, 0, -3750.1733, -633.4349, 7.4456);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 34, 0, -3747.3201, -630.6317, 7.1705);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 35, 0, -3744.4668, -627.8284, 6.8954);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 36, 0, -3741.6135, -625.0251, 6.5511);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 37, 0, -3738.7603, -622.2218, 6.2274);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 38, 0, -3735.9070, -619.4185, 5.9967);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 39, 0, -3733.0537, -616.6152, 5.6694);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 40, 0, -3730.2004, -613.8120, 5.4536);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 41, 0, -3727.3472, -611.0087, 5.3977);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 42, 0, -3724.4939, -608.2054, 5.3419);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 43, 0, -3721.6404, -605.4022, 5.2855);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 44, 0, -3718.7871, -602.5990, 5.1971);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2755, 4411, 45, 0, -3718.2563, -602.0776, 5.1807);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 0, 0, -3718.2563, -602.0776, 5.1807);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 1, 0, -3718.7871, -602.5990, 5.1971);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 2, 0, -3721.6404, -605.4022, 5.2855);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 3, 0, -3724.4939, -608.2054, 5.3419);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 4, 0, -3727.3472, -611.0087, 5.3977);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 5, 0, -3730.2004, -613.8120, 5.4536);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 6, 0, -3733.0537, -616.6152, 5.6694);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 7, 0, -3735.9070, -619.4185, 5.9967);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 8, 0, -3738.7603, -622.2218, 6.2274);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 9, 0, -3741.6135, -625.0251, 6.5511);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 10, 0, -3744.4668, -627.8284, 6.8954);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 11, 0, -3747.3201, -630.6317, 7.1705);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 12, 0, -3750.1733, -633.4349, 7.4456);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 13, 0, -3753.0266, -636.2382, 7.7207);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 14, 0, -3755.8799, -639.0415, 7.9957);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 15, 0, -3758.7332, -641.8448, 8.2707);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 16, 0, -3757.7715, -645.7275, 8.3187);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 17, 0, -3756.8096, -649.6101, 8.3849);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 18, 0, -3755.8479, -653.4927, 8.4511);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 19, 0, -3754.8860, -657.3754, 8.5173);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 20, 0, -3753.9243, -661.2580, 8.5741);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 21, 0, -3752.9624, -665.1406, 8.6142);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 22, 0, -3752.0007, -669.0233, 8.6676);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 23, 0, -3751.0388, -672.9059, 8.7252);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 24, 0, -3750.0769, -676.7885, 8.7828);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 25, 0, -3749.1150, -680.6711, 8.8351);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 26, 0, -3748.1531, -684.5538, 8.8778);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 27, 0, -3747.1912, -688.4364, 8.9204);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 28, 0, -3746.2292, -692.3190, 8.8444);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 29, 0, -3745.2673, -696.2017, 8.7022);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 30, 0, -3744.3054, -700.0843, 8.5600);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 31, 0, -3743.3435, -703.9669, 8.4178);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 32, 0, -3742.3816, -707.8495, 8.2747);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 33, 0, -3742.4309, -711.8492, 8.2560);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 34, 0, -3742.4800, -715.8489, 8.2811);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 35, 0, -3742.5293, -719.8486, 8.3061);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 36, 0, -3742.5786, -723.8483, 8.3311);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 37, 0, -3742.6279, -727.8480, 8.3214);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 38, 0, -3742.6772, -731.8477, 8.2609);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 39, 0, -3743.0078, -735.8340, 7.5060);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 40, 0, -3743.3381, -739.8204, 8.2925);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 41, 0, -3743.6687, -743.8067, 8.2464);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 42, 0, -3743.9990, -747.7930, 8.4891);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 43, 0, -3744.3293, -751.7794, 8.7953);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 44, 0, -3744.6597, -755.7657, 9.1571);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4411, 2755, 45, 0, -3744.9900, -759.7520, 9.6740);

-- ---- Zeppelin, Horde (Cloudkisser)dock (m571)  deck 1971 <-> dock 4412 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4412, 'Zeppelin, Horde (Cloudkisser)dock', 571, 1986.6572, -6086.9927, 14.1464, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1971, 4412, 3, 0, 5.00, 0, 0, 1, 0, 0, 0),
(4412, 1971, 3, 0, 5.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(3216, 4412, 1, 0, 96.55, 0, 0, 1, 60, 0, 0),
(4412, 3216, 1, 0, 96.55, 0, 0, 1, 60, 0, 0);
-- walk path 3216 -> 4412 (fwd 25 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 0, 571, 1948.5400, -6146.6299, 24.2777);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 1, 571, 1947.1439, -6142.8813, 24.2102);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 2, 571, 1945.7476, -6139.1328, 24.2064);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 3, 571, 1945.7883, -6135.1328, 24.2853);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 4, 571, 1945.8291, -6131.1328, 24.2423);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 5, 571, 1945.8699, -6127.1328, 24.0988);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 6, 571, 1945.9108, -6123.1328, 23.8892);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 7, 571, 1945.9515, -6119.1328, 23.5646);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 8, 571, 1945.9924, -6115.1328, 23.3405);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 9, 571, 1946.0332, -6111.1328, 23.1671);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 10, 571, 1946.0741, -6107.1328, 22.3951);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 11, 571, 1946.1150, -6103.1328, 21.3190);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 12, 571, 1946.1556, -6099.1328, 21.0329);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 13, 571, 1949.1194, -6096.4463, 20.7945);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 14, 571, 1952.0831, -6093.7598, 20.4713);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 15, 571, 1955.0469, -6091.0732, 20.1550);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 16, 571, 1958.0106, -6088.3872, 19.7778);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 17, 571, 1960.9744, -6085.7007, 19.3078);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 18, 571, 1963.9382, -6083.0146, 18.4257);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 19, 571, 1967.8984, -6082.4521, 18.2005);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 20, 571, 1971.8588, -6081.8901, 17.1600);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 21, 571, 1975.6403, -6083.1938, 16.5544);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 22, 571, 1979.4218, -6084.4976, 16.0865);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 23, 571, 1983.2032, -6085.8018, 15.2443);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3216, 4412, 24, 571, 1986.6572, -6086.9927, 14.1464);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 0, 571, 1986.6572, -6086.9927, 14.1464);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 1, 571, 1983.2032, -6085.8018, 15.2443);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 2, 571, 1979.4218, -6084.4976, 16.0865);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 3, 571, 1975.6403, -6083.1938, 16.5544);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 4, 571, 1971.8588, -6081.8901, 17.1600);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 5, 571, 1967.8984, -6082.4521, 18.2005);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 6, 571, 1963.9382, -6083.0146, 18.4257);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 7, 571, 1960.9744, -6085.7007, 19.3078);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 8, 571, 1958.0106, -6088.3872, 19.7778);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 9, 571, 1955.0469, -6091.0732, 20.1550);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 10, 571, 1952.0831, -6093.7598, 20.4713);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 11, 571, 1949.1194, -6096.4463, 20.7945);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 12, 571, 1946.1556, -6099.1328, 21.0329);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 13, 571, 1946.1150, -6103.1328, 21.3190);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 14, 571, 1946.0741, -6107.1328, 22.3951);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 15, 571, 1946.0332, -6111.1328, 23.1671);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 16, 571, 1945.9924, -6115.1328, 23.3405);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 17, 571, 1945.9515, -6119.1328, 23.5646);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 18, 571, 1945.9108, -6123.1328, 23.8892);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 19, 571, 1945.8699, -6127.1328, 24.0988);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 20, 571, 1945.8291, -6131.1328, 24.2423);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 21, 571, 1945.7883, -6135.1328, 24.2853);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 22, 571, 1945.7476, -6139.1328, 24.2064);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 23, 571, 1947.1439, -6142.8813, 24.2102);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4412, 3216, 24, 571, 1948.5400, -6146.6299, 24.2777);

-- ---- Zeppelin, Horde (Cloudkisser)dock (m0)  deck 1972 <-> dock 4413 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4413, 'Zeppelin, Horde (Cloudkisser)dock', 0, 2053.4412, 377.6721, 39.8026, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1972, 4413, 3, 0, 5.00, 0, 0, 1, 0, 0, 0),
(4413, 1972, 3, 0, 5.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(834, 4413, 1, 0, 171.28, 0, 0, 1, 60, 0, 0),
(4413, 834, 1, 0, 171.28, 0, 0, 1, 60, 0, 0);
-- walk path 834 -> 4413 (fwd 44 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 0, 0, 1951.3199, 244.9320, 42.5510);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 1, 0, 1953.8584, 248.0233, 42.5230);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 2, 0, 1956.3969, 251.1146, 42.0680);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 3, 0, 1958.9353, 254.2059, 41.0496);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 4, 0, 1961.4738, 257.2972, 39.6703);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 5, 0, 1964.0122, 260.3885, 38.3685);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 6, 0, 1966.5507, 263.4798, 37.9475);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 7, 0, 1969.0891, 266.5711, 38.1534);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 8, 0, 1971.6276, 269.6624, 38.8919);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 9, 0, 1974.1660, 272.7537, 40.1884);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 10, 0, 1976.7045, 275.8450, 41.6743);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 11, 0, 1979.2429, 278.9363, 42.7823);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 12, 0, 1981.7814, 282.0276, 43.8599);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 13, 0, 1984.3198, 285.1189, 44.8497);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 14, 0, 1986.8583, 288.2102, 45.2926);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 15, 0, 1989.3967, 291.3015, 45.1811);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 16, 0, 1991.9352, 294.3927, 44.8971);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 17, 0, 1994.4736, 297.4840, 44.5638);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 18, 0, 1997.0121, 300.5753, 44.2421);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 19, 0, 1999.5505, 303.6666, 43.9206);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 20, 0, 2002.0890, 306.7578, 43.6480);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 21, 0, 2004.6274, 309.8491, 43.4577);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 22, 0, 2007.1659, 312.9404, 43.3958);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 23, 0, 2009.7043, 316.0317, 43.5008);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 24, 0, 2012.2428, 319.1230, 43.7343);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 25, 0, 2014.7812, 322.2142, 44.0383);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 26, 0, 2017.3197, 325.3055, 44.3711);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 27, 0, 2019.8582, 328.3968, 44.7118);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 28, 0, 2022.3966, 331.4881, 44.7993);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 29, 0, 2024.9351, 334.5793, 44.8699);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 30, 0, 2027.4735, 337.6706, 44.9597);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 31, 0, 2030.0121, 340.7619, 45.2381);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 32, 0, 2032.5505, 343.8532, 45.8042);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 33, 0, 2035.0891, 346.9445, 46.2187);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 34, 0, 2037.6277, 350.0357, 46.4901);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 35, 0, 2040.1661, 353.1270, 46.4377);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 36, 0, 2042.7047, 356.2183, 46.0177);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 37, 0, 2045.2432, 359.3096, 45.1756);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 38, 0, 2047.7817, 362.4008, 43.8517);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 39, 0, 2050.3201, 365.4923, 42.5259);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 40, 0, 2051.3130, 369.3672, 41.0940);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 41, 0, 2052.3059, 373.2419, 40.1458);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 42, 0, 2053.2988, 377.1167, 39.8351);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (834, 4413, 43, 0, 2053.4412, 377.6721, 39.8026);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 0, 0, 2053.4412, 377.6721, 39.8026);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 1, 0, 2053.2988, 377.1167, 39.8351);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 2, 0, 2052.3059, 373.2419, 40.1458);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 3, 0, 2051.3130, 369.3672, 41.0940);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 4, 0, 2050.3201, 365.4923, 42.5259);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 5, 0, 2047.7817, 362.4008, 43.8517);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 6, 0, 2045.2432, 359.3096, 45.1756);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 7, 0, 2042.7047, 356.2183, 46.0177);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 8, 0, 2040.1661, 353.1270, 46.4377);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 9, 0, 2037.6277, 350.0357, 46.4901);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 10, 0, 2035.0891, 346.9445, 46.2187);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 11, 0, 2032.5505, 343.8532, 45.8042);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 12, 0, 2030.0121, 340.7619, 45.2381);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 13, 0, 2027.4735, 337.6706, 44.9597);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 14, 0, 2024.9351, 334.5793, 44.8699);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 15, 0, 2022.3966, 331.4881, 44.7993);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 16, 0, 2019.8582, 328.3968, 44.7118);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 17, 0, 2017.3197, 325.3055, 44.3711);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 18, 0, 2014.7812, 322.2142, 44.0383);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 19, 0, 2012.2428, 319.1230, 43.7343);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 20, 0, 2009.7043, 316.0317, 43.5008);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 21, 0, 2007.1659, 312.9404, 43.3958);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 22, 0, 2004.6274, 309.8491, 43.4577);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 23, 0, 2002.0890, 306.7578, 43.6480);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 24, 0, 1999.5505, 303.6666, 43.9206);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 25, 0, 1997.0121, 300.5753, 44.2421);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 26, 0, 1994.4736, 297.4840, 44.5638);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 27, 0, 1991.9352, 294.3927, 44.8971);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 28, 0, 1989.3967, 291.3015, 45.1811);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 29, 0, 1986.8583, 288.2102, 45.2926);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 30, 0, 1984.3198, 285.1189, 44.8497);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 31, 0, 1981.7814, 282.0276, 43.8599);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 32, 0, 1979.2429, 278.9363, 42.7823);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 33, 0, 1976.7045, 275.8450, 41.6743);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 34, 0, 1974.1660, 272.7537, 40.1884);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 35, 0, 1971.6276, 269.6624, 38.8919);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 36, 0, 1969.0891, 266.5711, 38.1534);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 37, 0, 1966.5507, 263.4798, 37.9475);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 38, 0, 1964.0122, 260.3885, 38.3685);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 39, 0, 1961.4738, 257.2972, 39.6703);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 40, 0, 1958.9353, 254.2059, 41.0496);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 41, 0, 1956.3969, 251.1146, 42.0680);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 42, 0, 1953.8584, 248.0233, 42.5230);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4413, 834, 43, 0, 1951.3199, 244.9320, 42.5510);

-- ---- Zeppelin, Horde (The Mighty Wind)dock (m1)  deck 1981 <-> dock 4414 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4414, 'Zeppelin, Horde (The Mighty Wind)dock', 1, 1185.9830, -4149.1162, 21.1732, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1981, 4414, 3, 0, 5.00, 0, 0, 1, 0, 0, 0),
(4414, 1981, 3, 0, 5.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2085, 4414, 1, 0, 106.66, 0, 0, 1, 60, 0, 0),
(4414, 2085, 1, 0, 106.66, 0, 0, 1, 60, 0, 0);
-- walk path 2085 -> 4414 (fwd 28 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 0, 1, 1206.1200, -4251.7100, 24.2996);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 1, 1, 1204.5920, -4248.0132, 24.5181);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 2, 1, 1203.0641, -4244.3164, 24.7834);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 3, 1, 1201.5361, -4240.6196, 24.9906);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 4, 1, 1200.0082, -4236.9229, 25.1355);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 5, 1, 1198.4802, -4233.2261, 25.1923);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 6, 1, 1196.9523, -4229.5293, 25.1557);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 7, 1, 1195.4243, -4225.8325, 24.9636);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 8, 1, 1193.8961, -4222.1357, 24.3495);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 9, 1, 1193.4652, -4218.1592, 22.6348);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 10, 1, 1193.0343, -4214.1826, 21.9375);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 11, 1, 1192.6034, -4210.2061, 21.6494);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 12, 1, 1192.1725, -4206.2295, 21.4977);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 13, 1, 1191.7416, -4202.2529, 21.2949);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 14, 1, 1191.3107, -4198.2764, 21.1887);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 15, 1, 1190.8798, -4194.2998, 21.6125);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 16, 1, 1190.4487, -4190.3232, 23.6161);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 17, 1, 1190.0178, -4186.3467, 23.0694);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 18, 1, 1189.5868, -4182.3701, 22.8440);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 19, 1, 1189.1559, -4178.3936, 21.7748);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 20, 1, 1188.7250, -4174.4170, 21.5660);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 21, 1, 1188.2939, -4170.4404, 21.4053);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 22, 1, 1187.8630, -4166.4639, 21.4053);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 23, 1, 1187.4321, -4162.4873, 21.3067);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 24, 1, 1187.0011, -4158.5107, 21.3972);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 25, 1, 1186.5702, -4154.5342, 21.0651);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 26, 1, 1186.1393, -4150.5576, 21.1213);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2085, 4414, 27, 1, 1185.9830, -4149.1162, 21.1732);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 0, 1, 1185.9830, -4149.1162, 21.1732);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 1, 1, 1186.1393, -4150.5576, 21.1213);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 2, 1, 1186.5702, -4154.5342, 21.0651);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 3, 1, 1187.0011, -4158.5107, 21.3972);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 4, 1, 1187.4321, -4162.4873, 21.3067);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 5, 1, 1187.8630, -4166.4639, 21.4053);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 6, 1, 1188.2939, -4170.4404, 21.4053);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 7, 1, 1188.7250, -4174.4170, 21.5660);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 8, 1, 1189.1559, -4178.3936, 21.7748);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 9, 1, 1189.5868, -4182.3701, 22.8440);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 10, 1, 1190.0178, -4186.3467, 23.0694);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 11, 1, 1190.4487, -4190.3232, 23.6161);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 12, 1, 1190.8798, -4194.2998, 21.6125);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 13, 1, 1191.3107, -4198.2764, 21.1887);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 14, 1, 1191.7416, -4202.2529, 21.2949);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 15, 1, 1192.1725, -4206.2295, 21.4977);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 16, 1, 1192.6034, -4210.2061, 21.6494);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 17, 1, 1193.0343, -4214.1826, 21.9375);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 18, 1, 1193.4652, -4218.1592, 22.6348);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 19, 1, 1193.8961, -4222.1357, 24.3495);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 20, 1, 1195.4243, -4225.8325, 24.9636);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 21, 1, 1196.9523, -4229.5293, 25.1557);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 22, 1, 1198.4802, -4233.2261, 25.1923);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 23, 1, 1200.0082, -4236.9229, 25.1355);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 24, 1, 1201.5361, -4240.6196, 24.9906);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 25, 1, 1203.0641, -4244.3164, 24.7834);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 26, 1, 1204.5920, -4248.0132, 24.5181);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4414, 2085, 27, 1, 1206.1200, -4251.7100, 24.2996);

-- ---- Turtle (Walker of Waves)dock (m571)  deck 2000 <-> dock 4417 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4417, 'Turtle (Walker of Waves)dock', 571, 2838.0056, 4020.4209, 4.8915, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2000, 4417, 3, 0, 21.00, 0, 0, 1, 0, 0, 0),
(4417, 2000, 3, 0, 21.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(9, 4417, 1, 0, 84.29, 0, 0, 1, 60, 0, 0),
(4417, 9, 1, 0, 84.29, 0, 0, 1, 60, 0, 0);
-- walk path 9 -> 4417 (fwd 22 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 0, 571, 2917.2100, 4043.4399, 1.8677);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 1, 571, 2914.1914, 4040.8154, 1.6785);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 2, 571, 2911.1724, 4038.1914, 1.4085);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 3, 571, 2907.1987, 4037.7329, 1.2773);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 4, 571, 2903.2251, 4037.2744, 1.2209);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 5, 571, 2899.2515, 4036.8159, 1.3387);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 6, 571, 2895.2778, 4036.3574, 1.8153);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 7, 571, 2891.3042, 4035.8989, 2.6261);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 8, 571, 2887.3306, 4035.4404, 3.5512);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 9, 571, 2883.3569, 4034.9819, 4.4303);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 10, 571, 2879.3833, 4034.5237, 5.1499);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 11, 571, 2875.4097, 4034.0652, 5.4134);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 12, 571, 2871.4863, 4033.2859, 5.0581);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 13, 571, 2867.7524, 4031.8511, 5.0249);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 14, 571, 2864.0186, 4030.4163, 4.9385);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 15, 571, 2860.2847, 4028.9814, 4.9153);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 16, 571, 2856.5508, 4027.5466, 4.9657);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 17, 571, 2852.8169, 4026.1121, 4.9880);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 18, 571, 2849.0830, 4024.6772, 4.9166);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 19, 571, 2845.3491, 4023.2427, 4.8299);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 20, 571, 2841.6152, 4021.8079, 4.8422);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (9, 4417, 21, 571, 2838.0056, 4020.4209, 4.8915);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 0, 571, 2838.0056, 4020.4209, 4.8915);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 1, 571, 2841.6152, 4021.8079, 4.8422);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 2, 571, 2845.3491, 4023.2427, 4.8299);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 3, 571, 2849.0830, 4024.6772, 4.9166);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 4, 571, 2852.8169, 4026.1121, 4.9880);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 5, 571, 2856.5508, 4027.5466, 4.9657);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 6, 571, 2860.2847, 4028.9814, 4.9153);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 7, 571, 2864.0186, 4030.4163, 4.9385);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 8, 571, 2867.7524, 4031.8511, 5.0249);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 9, 571, 2871.4863, 4033.2859, 5.0581);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 10, 571, 2875.4097, 4034.0652, 5.4134);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 11, 571, 2879.3833, 4034.5237, 5.1499);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 12, 571, 2883.3569, 4034.9819, 4.4303);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 13, 571, 2887.3306, 4035.4404, 3.5512);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 14, 571, 2891.3042, 4035.8989, 2.6261);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 15, 571, 2895.2778, 4036.3574, 1.8153);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 16, 571, 2899.2515, 4036.8159, 1.3387);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 17, 571, 2903.2251, 4037.2744, 1.2209);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 18, 571, 2907.1987, 4037.7329, 1.2773);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 19, 571, 2911.1724, 4038.1914, 1.4085);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 20, 571, 2914.1914, 4040.8154, 1.6785);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4417, 9, 21, 571, 2917.2100, 4043.4399, 1.8677);

-- ---- Turtle (Green Island)dock (m571)  deck 2008 <-> dock 4418 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4418, 'Turtle (Green Island)dock', 571, 790.8572, -2817.8303, 4.6385, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2008, 4418, 3, 0, 21.00, 0, 0, 1, 0, 0, 0),
(4418, 2008, 3, 0, 21.00, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(190, 4418, 1, 0, 72.81, 0, 0, 1, 60, 0, 0),
(4418, 190, 1, 0, 72.81, 0, 0, 1, 60, 0, 0);
-- walk path 190 -> 4418 (fwd 20 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 0, 571, 787.7550, -2889.0601, 6.4920);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 1, 571, 787.9805, -2885.0664, 5.4066);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 2, 571, 788.2060, -2881.0728, 4.5682);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 3, 571, 788.4316, -2877.0791, 3.8160);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 4, 571, 788.5116, -2873.0798, 3.2344);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 5, 571, 788.5916, -2869.0806, 2.9986);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 6, 571, 788.6716, -2865.0813, 3.3826);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 7, 571, 788.7516, -2861.0820, 3.8654);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 8, 571, 788.8316, -2857.0828, 4.4271);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 9, 571, 786.3955, -2853.9102, 4.8276);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 10, 571, 786.8864, -2849.9404, 4.8108);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 11, 571, 787.3773, -2845.9707, 4.7489);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 12, 571, 787.8682, -2842.0010, 4.7159);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 13, 571, 788.3591, -2838.0312, 4.7210);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 14, 571, 788.8500, -2834.0615, 4.7450);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 15, 571, 789.3409, -2830.0918, 4.7107);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 16, 571, 789.8318, -2826.1221, 4.6199);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 17, 571, 790.3228, -2822.1523, 4.5858);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 18, 571, 790.8137, -2818.1826, 4.6342);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (190, 4418, 19, 571, 790.8572, -2817.8303, 4.6385);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 0, 571, 790.8572, -2817.8303, 4.6385);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 1, 571, 790.8137, -2818.1826, 4.6342);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 2, 571, 790.3228, -2822.1523, 4.5858);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 3, 571, 789.8318, -2826.1221, 4.6199);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 4, 571, 789.3409, -2830.0918, 4.7107);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 5, 571, 788.8500, -2834.0615, 4.7450);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 6, 571, 788.3591, -2838.0312, 4.7210);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 7, 571, 787.8682, -2842.0010, 4.7159);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 8, 571, 787.3773, -2845.9707, 4.7489);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 9, 571, 786.8864, -2849.9404, 4.8108);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 10, 571, 786.3955, -2853.9102, 4.8276);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 11, 571, 788.8316, -2857.0828, 4.4271);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 12, 571, 788.7516, -2861.0820, 3.8654);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 13, 571, 788.6716, -2865.0813, 3.3826);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 14, 571, 788.5916, -2869.0806, 2.9986);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 15, 571, 788.5116, -2873.0798, 3.2344);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 16, 571, 788.4316, -2877.0791, 3.8160);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 17, 571, 788.2060, -2881.0728, 4.5682);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 18, 571, 787.9805, -2885.0664, 5.4066);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4418, 190, 19, 571, 787.7550, -2889.0601, 6.4920);

-- ---- Zeppelin, Horde (The Mighty Wind)dock (m571)  deck 1980 <-> dock 4415 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4415, 'Zeppelin, Horde (The Mighty Wind)dock', 571, 2836.1113, 6184.2660, 121.9065, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1980, 4415, 3, 0, 3.61, 0, 0, 1, 0, 0, 0),
(4415, 1980, 3, 0, 3.61, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(2592, 4415, 1, 0, 18.38, 0, 0, 1, 60, 0, 0),
(4415, 2592, 1, 0, 18.38, 0, 0, 1, 60, 0, 0);
-- walk path 2592 -> 4415 (fwd 2 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2592, 4415, 0, 571, 2822.7000, 6171.7000, 122.1000);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (2592, 4415, 1, 571, 2836.1113, 6184.2660, 121.9065);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4415, 2592, 0, 571, 2836.1113, 6184.2660, 121.9065);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4415, 2592, 1, 571, 2822.7000, 6171.7000, 122.1000);

-- ---- Ship, Night Elf (Feathermoon Ferry)dock (m1)  deck 1965 <-> dock 4416 ----
INSERT INTO `playerbots_travelnode` (`id`,`name`,`map_id`,`x`,`y`,`z`,`linked`) VALUES (4416, 'Ship, Night Elf (Feathermoon Ferry)dock', 1, -4347.4450, 2426.5840, 6.7632, 1) ON DUPLICATE KEY UPDATE x=VALUES(x), y=VALUES(y), z=VALUES(z), linked=VALUES(linked);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(1965, 4416, 3, 0, 15.43, 0, 0, 1, 0, 0, 0),
(4416, 1965, 3, 0, 15.43, 0, 0, 1, 0, 0, 0);
INSERT IGNORE INTO `playerbots_travelnode_link` (`node_id`,`to_node_id`,`type`,`object`,`distance`,`swim_distance`,`extra_cost`,`calculated`,`max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES
(3074, 4416, 1, 0, 191.14, 0, 0, 1, 60, 0, 0),
(4416, 3074, 1, 0, 191.14, 0, 0, 1, 60, 0, 0);
-- walk path 3074 -> 4416 (fwd 2 pts) + reverse
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3074, 4416, 0, 1, -4526.5000, 2360.2000, -1.4000);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (3074, 4416, 1, 1, -4347.4450, 2426.5840, 6.7632);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4416, 3074, 0, 1, -4347.4450, 2426.5840, 6.7632);
INSERT IGNORE INTO `playerbots_travelnode_path` (`node_id`,`to_node_id`,`nr`,`map_id`,`x`,`y`,`z`) VALUES (4416, 3074, 1, 1, -4526.5000, 2360.2000, -1.4000);

-- ---- fix existing dock node 4170 (Ship, Icebreaker (Northspear)dock m571) ----
UPDATE `playerbots_travelnode` SET x=584.0140, y=-5101.8599, z=5.2604 WHERE `id`=4170;

-- ---- fix existing dock node 4327 (Turtle (Walker of Waves)dock m571) ----
UPDATE `playerbots_travelnode` SET x=2636.1606, y=940.0406, z=5.1212 WHERE `id`=4327;

-- ---- fix existing dock node 4326 (Turtle (Green Island)dock m571) ----
UPDATE `playerbots_travelnode` SET x=2648.6113, y=843.3422, z=5.0597 WHERE `id`=4326;
