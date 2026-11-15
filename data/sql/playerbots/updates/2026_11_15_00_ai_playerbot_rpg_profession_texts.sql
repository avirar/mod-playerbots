-- #########################################################
-- Playerbots - Add do professions and recipe learning texts
-- Localized for all WotLK locales (koKR, frFR, deDE, zhCN,
-- zhTW, esES, esMX, ruRU)
-- #########################################################

DELETE FROM ai_playerbot_texts WHERE name IN (
    'rpg_professions_none_error',
    'rpg_professions_not_learned',
    'rpg_recipe_for_master',
    'rpg_unknown_status_error'
);

DELETE FROM ai_playerbot_texts_chance WHERE name IN (
    'rpg_professions_none_error',
    'rpg_professions_not_learned',
    'rpg_recipe_for_master',
    'rpg_unknown_status_error'
);

INSERT INTO `ai_playerbot_texts`
    (`id`, `name`, `text`, `say_type`, `reply_type`,
     `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`,
     `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8`)
VALUES
(1948,
 'rpg_professions_none_error',
 'I have no craft profession to work on.',
 0, 0,
 '제가 작업할 제작 전문 기술이 없습니다.',
 'Je n''ai aucune profession d''artisanat à travailler.',
 'Ich habe keinen Handwerksberuf, an dem ich arbeiten kann.',
 '我没有可以提升的制造专业。',
 '我沒有可以提升的製造專業。',
 'No tengo ninguna profesión de fabricación en la que trabajar.',
 'No tengo ninguna profesión de fabricación en la que trabajar.',
 'У меня нет ремесленной профессии, над которой можно работать.'),
(1949,
 'rpg_professions_not_learned',
 'I have not learned that profession.',
 0, 0,
 '저는 그 전문 기술을 배우지 않았습니다.',
 'Je n''ai pas appris ce métier.',
 'Ich habe diesen Beruf nicht erlernt.',
 '我没有学会那个专业。',
 '我沒有學會那個專業。',
 'No he aprendido esa profesión.',
 'No he aprendido esa profesión.',
 'Я не изучил эту профессию.'),
(1950,
 'rpg_recipe_for_master',
 'I have a recipe you can learn: %item',
 0, 0,
 '제가 배울 수 있는 제작법을 가지고 있습니다: %item',
 'J''ai une recette que vous pouvez apprendre : %item',
 'Ich habe ein Rezept, das du lernen kannst: %item',
 '我这里有一份你可以学习的配方：%item',
 '我這裡有一份你可以學習的配方：%item',
 'Tengo una receta que puedes aprender: %item',
 'Tengo una receta que puedes aprender: %item',
 'У меня есть рецепт, который ты можешь изучить: %item'),
(1766,
 'rpg_unknown_status_error',
 'Unknown rpg status. Options: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.',
 0, 0,
 '알 수 없는 RPG 상태입니다. 옵션: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.',
 'Statut RPG inconnu. Options: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.',
 'Unbekannter RPG-Status. Optionen: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.',
 '未知的RPG状态。选项: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions。',
 '未知的RPG狀態。選項: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions。',
 'Estado RPG desconocido. Opciones: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.',
 'Estado RPG desconocido. Opciones: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.',
 'Неизвестный RPG-статус. Опции: idle, rest, wander random, wander npc, go grind, go camp, do quest [<id>], travel flight, outdoor pvp, do gather, do professions.');

INSERT INTO ai_playerbot_texts_chance (name, probability) VALUES
('rpg_professions_none_error', 100),
('rpg_professions_not_learned', 100),
('rpg_recipe_for_master', 100),
('rpg_unknown_status_error', 100);
