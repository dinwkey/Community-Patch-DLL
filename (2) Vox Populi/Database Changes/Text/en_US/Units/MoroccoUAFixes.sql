-- ============================================
-- Morocco UA - Trade Plundering Fixes
-- Localization strings for diplomatic blockers
-- ============================================

INSERT OR IGNORE INTO Locale_en_US (Language, Tag, Text)
VALUES 
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_ALLIED', 
    'Cannot plunder trade route of allied nation.'),
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_VASSAL',
    'Cannot plunder trade route of vassal or overlord.'),
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_AFRAID',
    'We are too afraid of this civ to plunder their trade route.');
