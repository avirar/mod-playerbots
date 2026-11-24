-- #########################################################
-- Playerbots - announce the selected profession craft and plan
-- Localized for all WotLK locales (koKR, frFR, deDE, zhCN,
-- zhTW, esES, esMX, ruRU)
-- #########################################################

DELETE FROM ai_playerbot_texts WHERE name = 'rpg_professions_plan';
DELETE FROM ai_playerbot_texts_chance WHERE name = 'rpg_professions_plan';

INSERT INTO `ai_playerbot_texts`
    (`id`, `name`, `text`, `say_type`, `reply_type`,
     `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`,
     `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8`)
VALUES
(2038,
 'rpg_professions_plan',
 'Crafting %item. Plan: %steps.',
 0, 0,
 '%item 제작을 시작합니다. 계획: %steps.',
 'Je fabrique %item. Plan : %steps.',
 'Ich fertige %item an. Plan: %steps.',
 '正在制作%item。计划：%steps。',
 '正在製作%item。計劃：%steps。',
 'Fabricando %item. Plan: %steps.',
 'Fabricando %item. Plan: %steps.',
 'Создаю %item. План: %steps.');

INSERT INTO ai_playerbot_texts_chance (name, probability) VALUES
('rpg_professions_plan', 100);
