-- #########################################################
-- Playerbots - Add item usage and passive gathering texts
-- Localized for all WotLK locales (koKR, frFR, deDE, zhCN,
-- zhTW, esES, esMX, ruRU)
-- #########################################################

DELETE FROM ai_playerbot_texts WHERE name IN (
    'rpg_professions_gathering_passive_error',
    'item_usage_keep',
    'item_usage_future'
);

DELETE FROM ai_playerbot_texts_chance WHERE name IN (
    'rpg_professions_gathering_passive_error',
    'item_usage_keep',
    'item_usage_future'
);

INSERT INTO `ai_playerbot_texts`
    (`name`, `text`, `say_type`, `reply_type`,
     `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`,
     `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8`)
VALUES
('rpg_professions_gathering_passive_error',
 '%profession is a gathering profession; it is trained automatically.',
 0, 0,
 '%profession은(는) 채집 전문 기술입니다. 자동으로 훈련됩니다.',
 '%profession est un métier de récolte ; il est entraîné automatiquement.',
 '%profession ist ein Sammelberuf; er wird automatisch trainiert.',
 '%profession 是采集专业，会自动训练。',
 '%profession 是採集專業，會自動訓練。',
 '%profession es una profesión de recolección; se entrena automáticamente.',
 '%profession es una profesión de recolección; se entrena automáticamente.',
 '%profession — это профессия собирательства; она тренируется автоматически.'),
('item_usage_keep',
 'Keep',
 0, 0,
 '보관',
 'Conserver',
 'Behalten',
 '保留',
 '保留',
 'Conservar',
 'Conservar',
 'Хранить'),
('item_usage_future',
 'Future',
 0, 0,
 '향후',
 'Futur',
 'Später',
 '未来',
 '未來',
 'Futuro',
 'Futuro',
 'Будущее');
